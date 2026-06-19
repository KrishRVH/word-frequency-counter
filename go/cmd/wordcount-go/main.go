package main

import (
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"os"

	"word-frequency-counter-go/internal/wordcount"
)

type output struct {
	Total  uint64            `json:"total"`
	Unique int               `json:"unique"`
	Top    []wordcount.Entry `json:"top"`
}

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
	flag.Parse()

	if flag.NArg() != 1 || *topLimit <= 0 || *maxWord < 0 {
		return errors.New("usage: wordcount_go [--json] [--top N] [--max-word N] <file>")
	}
	path := flag.Arg(0)

	file, err := os.Open(flag.Arg(0))
	if err != nil {
		return fmt.Errorf("wordcount_go: cannot open %s: %w", path, err)
	}

	result, err := wordcount.Count(file, *maxWord)
	closeErr := file.Close()
	if err != nil {
		return fmt.Errorf("wordcount_go: cannot read %s: %w", path, err)
	}
	if closeErr != nil {
		return fmt.Errorf("wordcount_go: cannot close %s: %w", path, closeErr)
	}

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
