package wordcount

import (
	"cmp"
	"slices"
)

const estimatedBytesPerUniqueWord = 32

const (
	defaultMaxWord = 64
	maxWordLimit   = 1024
	minWord        = 4
)

// Entry is a ranked word and frequency pair.
type Entry struct {
	Word  string `json:"word"`
	Count uint64 `json:"count"`
}

// Result contains the aggregate count and normalized word frequencies.
type Result struct {
	Total  uint64            `json:"total"`
	Unique int               `json:"unique"`
	Counts map[string]uint64 `json:"-"`
}

// CountBytes counts all ASCII words in bytes.
func CountBytes(bytes []byte, maxWord int) Result {
	maxWord = NormalizeMaxWord(maxWord)
	counts := make(map[string]uint64, estimatedUniqueWords(bytes))
	word := make([]byte, 0, min(maxWord, defaultMaxWord))
	total := uint64(0)

	for _, byteValue := range bytes {
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

	return Result{Total: total, Unique: len(counts), Counts: counts}
}

func estimatedUniqueWords(bytes []byte) int {
	return len(bytes) / estimatedBytesPerUniqueWord
}

// Top returns at most limit entries in descending frequency and lexical tie order.
func Top(result Result, limit int) []Entry {
	entries := make([]Entry, 0, len(result.Counts))
	for word, count := range result.Counts {
		entries = append(entries, Entry{Word: word, Count: count})
	}

	slices.SortFunc(entries, func(left, right Entry) int {
		if order := cmp.Compare(right.Count, left.Count); order != 0 {
			return order
		}
		return cmp.Compare(left.Word, right.Word)
	})

	if limit >= 0 && len(entries) > limit {
		return entries[:limit]
	}
	return entries
}

// NormalizeMaxWord clamps maxWord to the supported range, using the default for zero.
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
	return (byteValue >= 'A' && byteValue <= 'Z') || (byteValue >= 'a' && byteValue <= 'z')
}

func lowerASCII(byteValue byte) byte {
	if byteValue >= 'A' && byteValue <= 'Z' {
		return byteValue + 32
	}
	return byteValue
}
