using System.Text;

namespace WordFrequencyCounter;

internal sealed record Entry(string Word, ulong Count);

internal sealed record Result(ulong Total, int Unique, IReadOnlyList<Entry> Top);

internal static class WordCounter {
    private const int DefaultMaxWord = 64;
    private const int EstimatedBytesPerUniqueWord = 32;
    private const int MaxWordLimit = 1024;
    private const int MinWord = 4;

    internal static Result CountFile(string path, int top, int maxWord) {
        byte[] bytes = File.ReadAllBytes(path);
        Accumulator accumulator = new(NormalizeMaxWord(maxWord), EstimatedUniqueWords(bytes));
        accumulator.AddBytes(bytes);
        return accumulator.Finish(top);
    }

    private static bool IsLetter(byte value) {
        return value is >= (byte) 'A' and <= (byte) 'Z' or >= (byte) 'a' and <= (byte) 'z';
    }

    private static byte LowerAscii(byte value) => value is >= (byte) 'A' and <= (byte) 'Z' ? (byte) (value + 32) : value;

    private static int EstimatedUniqueWords(ReadOnlySpan<byte> bytes) => bytes.Length / EstimatedBytesPerUniqueWord;

    private static int NormalizeMaxWord(int value) =>
        value switch {
            0 => DefaultMaxWord,
            < MinWord => MinWord,
            > MaxWordLimit => MaxWordLimit,
            _ => value,
        };

    private sealed class Accumulator {
        private readonly Dictionary<string, ulong> counts;
        private readonly int maxWord;
        private readonly StringBuilder word;
        private ulong total;

        internal Accumulator(int maxWord, int estimatedUniqueWords) {
            counts = new Dictionary<string, ulong>(estimatedUniqueWords, StringComparer.Ordinal);
            this.maxWord = maxWord;
            word = new StringBuilder(capacity: Math.Min(maxWord, DefaultMaxWord));
        }

        internal void AddBytes(ReadOnlySpan<byte> bytes) {
            foreach (byte value in bytes) {
                if (WordCounter.IsLetter(value)) {
                    if (word.Length < maxWord) {
                        word.Append((char) WordCounter.LowerAscii(value));
                    }
                    continue;
                }

                if (word.Length > 0) {
                    AddWord();
                }
            }
        }

        internal Result Finish(int top) {
            if (word.Length > 0) {
                AddWord();
            }

            List<Entry> entries = counts.Select(pair => new Entry(pair.Key, pair.Value)).ToList();
            entries.Sort(CompareEntries);
            if (entries.Count > top) {
                entries.RemoveRange(top, entries.Count - top);
            }

            return new Result(total, counts.Count, entries);
        }

        private static int CompareEntries(Entry left, Entry right) {
            int countOrder = right.Count.CompareTo(left.Count);
            return countOrder != 0 ? countOrder : string.CompareOrdinal(left.Word, right.Word);
        }

        private void AddWord() {
            string key = word.ToString();
            counts[key] = counts.TryGetValue(key, out ulong count) ? count + 1 : 1;
            total++;
            word.Clear();
        }
    }
}
