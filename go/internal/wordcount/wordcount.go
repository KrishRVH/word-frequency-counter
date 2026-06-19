package wordcount

import (
	"bufio"
	"io"
	"sort"
)

const readBufferSize = 64 * 1024

const (
	defaultMaxWord = 64
	maxWordLimit   = 1024
	minWord        = 4
)

type Entry struct {
	Word  string `json:"word"`
	Count uint64 `json:"count"`
}

type Result struct {
	Total  uint64            `json:"total"`
	Unique int               `json:"unique"`
	Counts map[string]uint64 `json:"-"`
}

func Count(reader io.Reader, maxWord int) (Result, error) {
	counts := make(map[string]uint64, 16_384)
	buffered := bufio.NewReaderSize(reader, readBufferSize)
	word := make([]byte, 0, 64)
	total := uint64(0)
	maxWord = NormalizeMaxWord(maxWord)

	for {
		byteValue, err := buffered.ReadByte()
		if err == io.EOF {
			break
		}
		if err != nil {
			return Result{}, err
		}

		if isLetter(byteValue) {
			if len(word) < maxWord {
				word = append(word, lowerASCII(byteValue))
			}
			continue
		}

		if len(word) > 0 {
			counts[string(word)]++
			total++
			word = word[:0]
		}
	}

	if len(word) > 0 {
		counts[string(word)]++
		total++
	}

	return Result{Total: total, Unique: len(counts), Counts: counts}, nil
}

func Top(result Result, limit int) []Entry {
	entries := make([]Entry, 0, len(result.Counts))
	for word, count := range result.Counts {
		entries = append(entries, Entry{Word: word, Count: count})
	}

	sort.Slice(entries, func(left int, right int) bool {
		if entries[left].Count != entries[right].Count {
			return entries[left].Count > entries[right].Count
		}
		return entries[left].Word < entries[right].Word
	})

	if limit >= 0 && len(entries) > limit {
		return entries[:limit]
	}
	return entries
}

func NormalizeMaxWord(maxWord int) int {
	switch {
	case maxWord == 0:
		return defaultMaxWord
	case maxWord < minWord:
		return minWord
	case maxWord > maxWordLimit:
		return maxWordLimit
	default:
		return maxWord
	}
}

func isLetter(byteValue byte) bool {
	lower := byteValue | 32
	return lower >= 'a' && lower <= 'z'
}

func lowerASCII(byteValue byte) byte {
	if byteValue >= 'A' && byteValue <= 'Z' {
		return byteValue + 32
	}
	return byteValue
}
