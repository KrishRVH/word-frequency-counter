using System.Text;

namespace WordFrequencyCounter;

internal sealed record Entry(string Word, ulong Count);

internal sealed record Result(ulong Total, int Unique, IReadOnlyList<Entry> Top);

internal static class WordCounter {
    private const int ReadBufferSize = 64 * 1024;

    internal static Result CountFile(string path, int top, int maxWord) {
        using FileStream stream = File.OpenRead(path);
        byte[] buffer = new byte[ReadBufferSize];
        Accumulator accumulator = new(maxWord);

        while (true) {
            int read = stream.Read(buffer);
            if (read == 0) {
                return accumulator.Finish(top);
            }

            accumulator.AddBytes(buffer.AsSpan(0, read));
        }
    }

    private static bool IsLetter(byte value) {
        return value is >= (byte) 'A' and <= (byte) 'Z' or >= (byte) 'a' and <= (byte) 'z';
    }

    private static byte LowerAscii(byte value) => value is >= (byte) 'A' and <= (byte) 'Z' ? (byte) (value + 32) : value;

    private sealed class Accumulator {
        private readonly Dictionary<string, ulong> counts = new(StringComparer.Ordinal);
        private readonly int maxWord;
        private readonly StringBuilder word;
        private ulong total;

        internal Accumulator(int maxWord) {
            this.maxWord = maxWord;
            word = new StringBuilder(capacity: Math.Min(maxWord, 64));
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

            List<Entry> entries = counts
                .Select(pair => new Entry(pair.Key, pair.Value))
                .OrderByDescending(entry => entry.Count)
                .ThenBy(entry => entry.Word, StringComparer.Ordinal)
                .Take(top)
                .ToList();

            return new Result(total, counts.Count, entries);
        }

        private void AddWord() {
            string key = word.ToString();
            counts[key] = counts.TryGetValue(key, out ulong count) ? count + 1 : 1;
            total++;
            word.Clear();
        }
    }
}
