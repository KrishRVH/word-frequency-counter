using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace WordFrequencyCounter;

internal static class WordCounter
{
    private const int DefaultMaxWord = 64;
    private const int EstimatedBytesPerUniqueWord = 32;
    private const int MaxWordLimit = 1024;
    private const int MinWord = 4;

    internal static Result CountBytes(ReadOnlySpan<byte> bytes, int top, int maxWord)
    {
        Accumulator accumulator = new(NormalizeMaxWord(maxWord), EstimatedUniqueWords(bytes));
        accumulator.AddBytes(bytes);
        return accumulator.Finish(top);
    }

    private static bool IsLetter(byte value) => value is (>= (byte) 'A' and <= (byte) 'Z') or (>= (byte) 'a' and <= (byte) 'z');

    private static byte LowerAscii(byte value) => value is >= (byte) 'A' and <= (byte) 'Z' ? (byte) (value + 32) : value;

    private static int EstimatedUniqueWords(ReadOnlySpan<byte> bytes) => bytes.Length / EstimatedBytesPerUniqueWord;

    private static int NormalizeMaxWord(int value) =>
        value switch
        {
            0 => DefaultMaxWord,
            < MinWord => MinWord,
            > MaxWordLimit => MaxWordLimit,
            _ => value,
        };

    private sealed class Accumulator
    {
        private readonly Dictionary<string, ulong> _counts;
        private readonly int _maxWord;
        private readonly StringBuilder _word;
        private ulong _total;

        internal Accumulator(int maxWord, int estimatedUniqueWords)
        {
            _counts = new Dictionary<string, ulong>(estimatedUniqueWords, StringComparer.Ordinal);
            _maxWord = maxWord;
            _word = new StringBuilder(capacity: Math.Min(maxWord, DefaultMaxWord));
        }

        internal void AddBytes(ReadOnlySpan<byte> bytes)
        {
            foreach (byte value in bytes)
            {
                if (IsLetter(value))
                {
                    if (_word.Length < _maxWord)
                    {
                        _word.Append((char) LowerAscii(value));
                    }
                    continue;
                }

                if (_word.Length > 0)
                {
                    AddWord();
                }
            }
        }

        internal Result Finish(int top)
        {
            if (_word.Length > 0)
            {
                AddWord();
            }

            List<Entry> entries = [.. _counts.Select(pair => new Entry(pair.Key, pair.Value))];
            entries.Sort(CompareEntries);
            if (entries.Count > top)
            {
                entries.RemoveRange(top, entries.Count - top);
            }

            return new Result(_total, _counts.Count, entries);
        }

        private static int CompareEntries(Entry left, Entry right)
        {
            int countOrder = right.Count.CompareTo(left.Count);
            return countOrder != 0 ? countOrder : string.CompareOrdinal(left.Word, right.Word);
        }

        private void AddWord()
        {
            string key = _word.ToString();
            _counts[key] = _counts.TryGetValue(key, out ulong count) ? count + 1 : 1;
            _total++;
            _word.Clear();
        }
    }
}
