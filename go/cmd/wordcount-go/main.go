package main

import (
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"os"
	"time"

	"word-frequency-counter-go/internal/wordcount"
)

type output struct {
	Total  uint64            `json:"total"`
	Unique int               `json:"unique"`
	Top    []wordcount.Entry `json:"top"`
}

const (
	checksumOffset uint32 = 2_166_136_261
	checksumPrime  uint32 = 16_777_619
)

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func run() error {
	jsonMode := flag.Bool("json", false, "print JSON")
	topLimit := flag.Int("top", 10, "number of entries to print")
	maxWord := flag.Int("max-word", 1024, "maximum stored word length")
	benchRuns := flag.Int("bench-runs", 0, "in-process benchmark repetitions")
	benchWarmups := flag.Int("bench-warmups", 0, "in-process benchmark warmups")
	flag.Parse()

	if flag.NArg() != 1 || *topLimit <= 0 || *maxWord < 0 || *benchRuns < 0 || *benchWarmups < 0 {
		return errors.New("usage: wordcount_go [--json] [--top N] [--max-word N] <file>")
	}
	path := flag.Arg(0)

	// #nosec G304 -- the CLI intentionally reads the user-supplied fixture path.
	bytes, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("wordcount_go: cannot read %s: %w", path, err)
	}

	if *benchRuns > 0 {
		printBench(bytes, *topLimit, *maxWord, *benchRuns, *benchWarmups)
		return nil
	}

	result := wordcount.CountBytes(bytes, *maxWord)
	report := output{
		Total:  result.Total,
		Unique: result.Unique,
		Top:    wordcount.Top(result, *topLimit),
	}

	if *jsonMode {
		encoder := json.NewEncoder(os.Stdout)
		encoder.SetEscapeHTML(false)
		return encoder.Encode(report)
	}

	fmt.Println("count word")
	for _, entry := range report.Top {
		fmt.Printf("%d %s\n", entry.Count, entry.Word)
	}
	fmt.Printf("total %d\nunique %d\n", report.Total, report.Unique)
	return nil
}

func printBench(bytes []byte, top, maxWord, runs, warmups int) {
	for range warmups {
		_ = checksum(wordcount.CountBytes(bytes, maxWord), top)
	}

	started := time.Now()
	checksumValue := checksumOffset
	for range runs {
		checksumValue = mixUint32(checksumValue, checksum(wordcount.CountBytes(bytes, maxWord), top))
	}
	meanMs := float64(time.Since(started).Nanoseconds()) / 1_000_000.0 / float64(runs)

	fmt.Printf("{\"mean_ms\":%.6f,\"checksum\":%d}\n", meanMs, checksumValue)
}

func checksum(result wordcount.Result, top int) uint32 {
	value := checksumOffset
	value = mixUint64(value, result.Total)
	// #nosec G115 -- Unique is len(counts), so it is non-negative and input-bounded.
	value = mixUint64(value, uint64(result.Unique))
	for _, entry := range wordcount.Top(result, top) {
		for index := range entry.Word {
			value = mixByte(value, entry.Word[index])
		}
		value = mixUint64(value, entry.Count)
	}
	return value
}

func mixByte(checksum uint32, value byte) uint32 {
	return (checksum ^ uint32(value)) * checksumPrime
}

func mixUint32(checksum, value uint32) uint32 {
	for range 4 {
		checksum = mixByte(checksum, byte(value&0xff))
		value >>= 8
	}
	return checksum
}

func mixUint64(checksum uint32, value uint64) uint32 {
	for range 8 {
		checksum = mixByte(checksum, byte(value&0xff))
		value >>= 8
	}
	return checksum
}
