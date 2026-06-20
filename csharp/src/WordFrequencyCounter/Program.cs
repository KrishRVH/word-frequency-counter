using System.Text.Json;

namespace WordFrequencyCounter;

internal static class Program {
    private static int Main(string[] args) {
        try {
            Options options = Options.Parse(args);
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
}

internal sealed record Options(string Path, int Top, int MaxWord, bool Json) {
    private const string Usage = "usage: wordcount_csharp [--json] [--top N] [--max-word N] <file>";

    internal static Options Parse(IReadOnlyList<string> args) {
        string? path = null;
        int top = 10;
        int maxWord = 1024;
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

        return new Options(path, top, maxWord, json);
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
