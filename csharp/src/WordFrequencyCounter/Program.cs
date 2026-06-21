using System.Globalization;
using System.Text.Json;

namespace WordFrequencyCounter;

internal static class Program {
    private static int Main(string[] args) {
        try {
            Options options = Options.Parse(args);
            if (options.BenchRuns > 0) {
                Console.Write(PrintBench(options));
                return 0;
            }

            Result result = WordCounter.CountFile(options.Path, options.Top, options.MaxWord);

            Console.Write(options.Json ? RenderJson(result) : RenderText(result));
            return 0;
        } catch (ArgumentException error) {
            Console.Error.WriteLine($"wordcount_csharp: {error.Message}");
            return 2;
        } catch (Exception error) when (error is IOException or UnauthorizedAccessException) {
            Console.Error.WriteLine($"wordcount_csharp: {error.Message}");
            return 1;
        }
    }

    private static string RenderJson(Result result) {
        return JsonSerializer.Serialize(
            new {
                total = result.Total,
                unique = result.Unique,
                top = result.Top.Select(entry => new { word = entry.Word, count = entry.Count }),
            }
        ) + Environment.NewLine;
    }

    private static string RenderText(Result result) {
        using StringWriter writer = new();
        writer.WriteLine("count word");
        foreach (Entry entry in result.Top) {
            writer.WriteLine($"{entry.Count} {entry.Word}");
        }
        writer.WriteLine($"total {result.Total}");
        writer.WriteLine($"unique {result.Unique}");
        return writer.ToString();
    }

    private static string PrintBench(Options options) {
        byte[] bytes = File.ReadAllBytes(options.Path);
        for (int index = 0; index < options.BenchWarmups; index++) {
            _ = Checksum(WordCounter.CountBytes(bytes, options.Top, options.MaxWord));
        }

        ulong checksum = 0;
        System.Diagnostics.Stopwatch stopwatch = System.Diagnostics.Stopwatch.StartNew();
        for (int index = 0; index < options.BenchRuns; index++) {
            checksum ^= Checksum(WordCounter.CountBytes(bytes, options.Top, options.MaxWord));
        }
        stopwatch.Stop();

        double meanMs = stopwatch.Elapsed.TotalMilliseconds / options.BenchRuns;
        string mean = meanMs.ToString("F6", CultureInfo.InvariantCulture);
        return $$"""{"mean_ms":{{mean}},"checksum":{{checksum}}}""" + Environment.NewLine;
    }

    private static ulong Checksum(Result result) {
        ulong checksum = result.Total ^ (ulong) result.Unique;
        foreach (Entry entry in result.Top) {
            checksum ^= entry.Count ^ (ulong) entry.Word.Length;
        }
        return checksum;
    }
}

internal sealed record Options(string Path, int Top, int MaxWord, int BenchRuns, int BenchWarmups, bool Json) {
    private const string Usage = "usage: wordcount_csharp [--json] [--top N] [--max-word N] <file>";

    internal static Options Parse(IReadOnlyList<string> args) {
        string? path = null;
        int top = 10;
        int maxWord = 1024;
        int benchRuns = 0;
        int benchWarmups = 0;
        bool json = false;

        for (int index = 0; index < args.Count; index++) {
            string arg = args[index];
            if (arg == "--json") {
                json = true;
            } else if (arg == "--top") {
                top = ParseValue(args, ++index, "--top");
            } else if (arg.StartsWith("--top=", StringComparison.Ordinal)) {
                top = ParseNumber(arg[6..], "--top");
            } else if (arg == "--max-word") {
                maxWord = ParseValue(args, ++index, "--max-word");
            } else if (arg.StartsWith("--max-word=", StringComparison.Ordinal)) {
                maxWord = ParseNumber(arg[11..], "--max-word");
            } else if (arg == "--bench-runs") {
                benchRuns = ParseValue(args, ++index, "--bench-runs");
            } else if (arg.StartsWith("--bench-runs=", StringComparison.Ordinal)) {
                benchRuns = ParseNumber(arg[13..], "--bench-runs");
            } else if (arg == "--bench-warmups") {
                benchWarmups = ParseValue(args, ++index, "--bench-warmups");
            } else if (arg.StartsWith("--bench-warmups=", StringComparison.Ordinal)) {
                benchWarmups = ParseNumber(arg[16..], "--bench-warmups");
            } else if (arg.StartsWith('-')) {
                throw new ArgumentException(Usage);
            } else if (path is null) {
                path = arg;
            } else {
                throw new ArgumentException(Usage);
            }
        }

        if (path is null || top <= 0) {
            throw new ArgumentException(Usage);
        }

        return new Options(path, top, maxWord, benchRuns, benchWarmups, json);
    }

    private static int ParseValue(IReadOnlyList<string> args, int index, string name) {
        if (index >= args.Count) {
            throw new ArgumentException($"{name} requires a value");
        }
        return ParseNumber(args[index], name);
    }

    private static int ParseNumber(string value, string name) {
        return value.Length > 0
            && value.All(static character => character is >= '0' and <= '9')
            && int.TryParse(value, out int parsed)
            ? parsed
            : throw new ArgumentException($"{name} must be a number");
    }
}
