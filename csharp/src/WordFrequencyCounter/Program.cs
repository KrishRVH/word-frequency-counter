using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace WordFrequencyCounter;

internal static class Program
{
    private const uint ChecksumOffset = 2_166_136_261;
    private const uint ChecksumPrime = 16_777_619;

    private static int Main(string[] args)
    {
        try
        {
            Options options = Options.Parse(args);
            byte[] bytes = File.ReadAllBytes(options.Path);
            if (options.BenchRuns > 0)
            {
                Console.Write(PrintBench(bytes, options));
                return 0;
            }

            Result result = WordCounter.CountBytes(bytes, options.Top, options.MaxWord);

            Console.Write(options.Json ? RenderJson(result) : RenderText(result));
            return 0;
        }
        catch (Exception error) when (error is ArgumentException or FormatException)
        {
            Console.Error.WriteLine($"wordcount_csharp: {error.Message}");
            return 2;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            Console.Error.WriteLine($"wordcount_csharp: {error.Message}");
            return 1;
        }
    }

    private static string RenderJson(Result result) => JsonSerializer.Serialize(
            new
            {
                total = result.Total,
                unique = result.Unique,
                top = result.Top.Select(entry => new { word = entry.Word, count = entry.Count }),
            }
        ) + Environment.NewLine;

    private static string RenderText(Result result)
    {
        using StringWriter writer = new();
        writer.WriteLine("count word");
        foreach (Entry entry in result.Top)
        {
            writer.Write(entry.Count.ToString(CultureInfo.InvariantCulture));
            writer.Write(' ');
            writer.WriteLine(entry.Word);
        }
        writer.Write("total ");
        writer.WriteLine(result.Total.ToString(CultureInfo.InvariantCulture));
        writer.Write("unique ");
        writer.WriteLine(result.Unique.ToString(CultureInfo.InvariantCulture));
        return writer.ToString();
    }

    private static string PrintBench(byte[] bytes, Options options)
    {
        for (int index = 0; index < options.BenchWarmups; index++)
        {
            _ = Checksum(WordCounter.CountBytes(bytes, options.Top, options.MaxWord));
        }

        uint checksum = ChecksumOffset;
        System.Diagnostics.Stopwatch stopwatch = System.Diagnostics.Stopwatch.StartNew();
        for (int index = 0; index < options.BenchRuns; index++)
        {
            checksum = MixUint32(checksum, Checksum(WordCounter.CountBytes(bytes, options.Top, options.MaxWord)));
        }
        stopwatch.Stop();

        double meanMs = stopwatch.Elapsed.TotalMilliseconds / options.BenchRuns;
        string mean = meanMs.ToString("F6", CultureInfo.InvariantCulture);
        return $$"""{"mean_ms":{{mean}},"checksum":{{checksum}}}""" + Environment.NewLine;
    }

    private static uint Checksum(Result result)
    {
        uint checksum = ChecksumOffset;
        checksum = MixUint64(checksum, result.Total);
        checksum = MixUint64(checksum, (ulong) result.Unique);
        foreach (Entry entry in result.Top)
        {
            foreach (char character in entry.Word)
            {
                checksum = MixByte(checksum, (byte) character);
            }
            checksum = MixUint64(checksum, entry.Count);
        }
        return checksum;
    }

    private static uint MixByte(uint checksum, byte value) => unchecked((checksum ^ value) * ChecksumPrime);

    private static uint MixUint32(uint checksum, uint value)
    {
        for (int index = 0; index < 4; index++)
        {
            checksum = MixByte(checksum, (byte) (value & 0xff));
            value >>= 8;
        }
        return checksum;
    }

    private static uint MixUint64(uint checksum, ulong value)
    {
        for (int index = 0; index < 8; index++)
        {
            checksum = MixByte(checksum, (byte) (value & 0xff));
            value >>= 8;
        }
        return checksum;
    }
}
