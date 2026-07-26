using System;
using System.Globalization;
using System.Linq;

namespace WordFrequencyCounter;

internal sealed record Options(string Path, int Top, int MaxWord, int BenchRuns, int BenchWarmups, bool Json)
{
    private const string Usage = "usage: wordcount_csharp [--json] [--top N] [--max-word N] <file>";

    internal static Options Parse(string[] args) => new Parser(args).Parse();

    private static int ParseNumber(string value, string name) => value.Length > 0
            && value.All(static character => character is >= '0' and <= '9')
            && int.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out int parsed)
                ? parsed
                : throw new ArgumentException($"{name} must be a number", nameof(value));

    private sealed class Parser
    {
        private readonly string[] _args;
        private int _benchRuns;
        private int _benchWarmups;
        private int _index;
        private bool _json;
        private int _maxWord = 1024;
        private string? _path;
        private int _top = 10;

        internal Parser(string[] args)
        {
            ArgumentNullException.ThrowIfNull(args);
            _args = args;
        }

        internal Options Parse()
        {
            while (_index < _args.Length)
            {
                ParseArgument(_args[_index++]);
            }

            return _path is null || _top <= 0 ? throw UsageError() : new Options(_path, _top, _maxWord, _benchRuns, _benchWarmups, _json);
        }

        private void ParseArgument(string arg)
        {
            if (string.Equals(arg, "--json", StringComparison.Ordinal))
            {
                _json = true;
            }
            else if (string.Equals(arg, "--top", StringComparison.Ordinal))
            {
                _top = ParseValue("--top");
            }
            else if (arg.StartsWith("--top=", StringComparison.Ordinal))
            {
                _top = ParseNumber(arg[6..], "--top");
            }
            else if (string.Equals(arg, "--max-word", StringComparison.Ordinal))
            {
                _maxWord = ParseValue("--max-word");
            }
            else if (arg.StartsWith("--max-word=", StringComparison.Ordinal))
            {
                _maxWord = ParseNumber(arg[11..], "--max-word");
            }
            else if (string.Equals(arg, "--bench-runs", StringComparison.Ordinal))
            {
                _benchRuns = ParseValue("--bench-runs");
            }
            else if (arg.StartsWith("--bench-runs=", StringComparison.Ordinal))
            {
                _benchRuns = ParseNumber(arg[13..], "--bench-runs");
            }
            else if (string.Equals(arg, "--bench-warmups", StringComparison.Ordinal))
            {
                _benchWarmups = ParseValue("--bench-warmups");
            }
            else if (arg.StartsWith("--bench-warmups=", StringComparison.Ordinal))
            {
                _benchWarmups = ParseNumber(arg[16..], "--bench-warmups");
            }
            else
            {
                ParsePath(arg);
            }
        }

        private int ParseValue(string name) => _index >= _args.Length
            ? throw new FormatException($"{name} requires a value")
            : ParseNumber(_args[_index++], name);

        private void ParsePath(string arg)
        {
            if (arg.StartsWith('-') || _path is not null)
            {
                throw UsageError();
            }

            _path = arg;
        }
    }

    private static FormatException UsageError() => new(Usage);
}
