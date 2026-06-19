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

    internal static Result CountBytes(ReadOnlySpan<byte> bytes, int top, int maxWord) {
        Accumulator accumulator = new(maxWord);
        accumulator.AddBytes(bytes);
        return accumulator.Finish(top);
    }

    private static bool IsLetter(byte value) {
        byte lower = (byte) (value | 32);
        return lower is >= (byte) 'a' and <= (byte) 'z';
    }

    private static byte LowerAscii(byte value) => value is >= (byte) 'A' and <= (byte) 'Z' ? (byte) (value + 32) : value;

    private sealed class Accumulator {
        private readonly Dictionary<string, ulong> counts = new(StringComparer.Ordinal);
        private readonly int maxWord;
        private readonly StringBuilder word;
        private bool inWord;
        private ulong total;

        internal Accumulator(int maxWord) {
            this.maxWord = maxWord;
            word = new StringBuilder(capacity: Math.Min(maxWord, 64));
        }

        internal void AddBytes(ReadOnlySpan<byte> bytes) {
            foreach (byte value in bytes) {
                if (IsLetter(value)) {
                    inWord = true;
                    if (word.Length < maxWord) {
                        word.Append((char) LowerAscii(value));
                    }
                    continue;
                }

                if (inWord) {
                    AddWord();
                }
            }
        }

        internal Result Finish(int top) {
            if (inWord) {
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
            inWord = false;
        }

        private static bool IsLetter(byte value) => WordCounter.IsLetter(value);

        private static byte LowerAscii(byte value) => WordCounter.LowerAscii(value);
    }
}
