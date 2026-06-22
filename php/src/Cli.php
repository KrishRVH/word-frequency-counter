<?php

declare(strict_types=1);

namespace WordFrequencyCounter;

use InvalidArgumentException;
use RuntimeException;
use Throwable;

final class Cli
{
    private const string USAGE = "usage: php/bin/wordcount [--json] [--top N] [--max-word N] <file>\n";
    private const int CHECKSUM_OFFSET = 2_166_136_261;
    private const int CHECKSUM_PRIME = 16_777_619;
    private const int CHECKSUM_MOD = 4_294_967_296;
    private const array NUMBER_OPTIONS = [
        'top' => ['--top', '--top='],
        'maxWord' => ['--max-word', '--max-word='],
        'benchRuns' => ['--bench-runs', '--bench-runs='],
        'benchWarmups' => ['--bench-warmups', '--bench-warmups='],
    ];

    /**
     * @param list<string> $arguments
     */
    public static function main(array $arguments, mixed $stdout, mixed $stderr): int
    {
        try {
            $options = self::parse($arguments);
            $counter = new WordCounter();
            if ($options->benchRuns > 0) {
                self::write($stdout, self::renderBench($counter, $options));

                return 0;
            }

            $result = $counter->countFile($options->path, $options->top, $options->maxWord);
            self::write($stdout, $options->json ? self::renderJson($result) : self::renderText($result));

            return 0;
        } catch (InvalidArgumentException) {
            self::write($stderr, self::USAGE);

            return 2;
        } catch (Throwable $error) {
            self::write($stderr, "wordcount_php: {$error->getMessage()}\n");

            return 1;
        }
    }

    /**
     * @return list<string>
     */
    public static function normalizeArguments(mixed $arguments): array
    {
        if (!is_array($arguments)) {
            return [];
        }

        return array_values(array_filter($arguments, 'is_string'));
    }

    /**
     * @param list<string> $arguments
     */
    private static function parse(array $arguments): CliOptions
    {
        $json = false;
        $numberOptions = [
            'top' => 10,
            'maxWord' => 1024,
            'benchRuns' => 0,
            'benchWarmups' => 0,
        ];
        $path = null;
        $argumentCount = count($arguments);

        for ($index = 1; $index < $argumentCount; $index++) {
            $argument = $arguments[$index];

            if ($argument === '--json') {
                $json = true;
                continue;
            }

            $numberOption = self::readConfiguredNumberOption($arguments, $argument, $index);
            if ($numberOption !== null) {
                $index = $numberOption['index'];
                $numberOptions[$numberOption['key']] = $numberOption['value'];
                continue;
            }

            if (str_starts_with($argument, '-')) {
                throw new InvalidArgumentException('unknown option');
            }

            if ($path === null) {
                $path = $argument;
                continue;
            }

            throw new InvalidArgumentException('too many file operands');
        }

        if ($path === null || $numberOptions['top'] <= 0) {
            throw new InvalidArgumentException('invalid options');
        }

        return new CliOptions(
            $path,
            $numberOptions['top'],
            $numberOptions['maxWord'],
            $numberOptions['benchRuns'],
            $numberOptions['benchWarmups'],
            $json,
        );
    }

    /**
     * @param list<string> $arguments
     *
     * @return array{index: int, key: string, value: int}|null
     */
    private static function readConfiguredNumberOption(
        array $arguments,
        string $argument,
        int $index,
    ): ?array {
        foreach (self::NUMBER_OPTIONS as $key => [$name, $prefix]) {
            $option = self::readNumberOption($arguments, $argument, $name, $prefix, $index);
            if ($option === null) {
                continue;
            }

            return ['index' => $option['index'], 'key' => $key, 'value' => $option['value']];
        }

        return null;
    }

    /**
     * @param list<string> $arguments
     *
     * @return array{index: int, value: int}|null
     */
    private static function readNumberOption(
        array $arguments,
        string $argument,
        string $name,
        string $prefix,
        int $index,
    ): ?array {
        if ($argument === $name) {
            $index++;

            return ['index' => $index, 'value' => self::parseNumber($arguments[$index] ?? null)];
        }

        if (str_starts_with($argument, $prefix)) {
            return ['index' => $index, 'value' => self::parseNumber(substr($argument, strlen($prefix)))];
        }

        return null;
    }

    private static function parseNumber(?string $value): int
    {
        if ($value === null || !ctype_digit($value)) {
            throw new InvalidArgumentException('expected numeric option value');
        }

        $parsed = filter_var($value, FILTER_VALIDATE_INT, ['options' => ['min_range' => 0]]);
        if (!is_int($parsed)) {
            throw new InvalidArgumentException('expected numeric option value');
        }

        return $parsed;
    }

    private static function renderJson(Result $result): string
    {
        return json_encode(
            [
                'total' => $result->total,
                'unique' => $result->unique,
                'top' => array_map(
                    static fn (Entry $entry): array => ['word' => $entry->word, 'count' => $entry->count],
                    $result->top,
                ),
            ],
            JSON_THROW_ON_ERROR | JSON_UNESCAPED_SLASHES,
        ) . "\n";
    }

    private static function renderText(Result $result): string
    {
        $lines = ['count word'];
        foreach ($result->top as $entry) {
            $lines[] = "{$entry->count} {$entry->word}";
        }
        $lines[] = "total {$result->total}";
        $lines[] = "unique {$result->unique}";

        return implode("\n", $lines) . "\n";
    }

    private static function renderBench(WordCounter $counter, CliOptions $options): string
    {
        $bytes = file_get_contents($options->path);
        if ($bytes === false) {
            throw new RuntimeException("cannot read {$options->path}");
        }

        $checksum = self::CHECKSUM_OFFSET;
        for ($index = 0; $index < $options->benchWarmups; $index++) {
            $checksum = self::mixUint32(
                $checksum,
                self::checksum($counter->countBytes($bytes, $options->top, $options->maxWord)),
            );
        }

        $checksum = self::CHECKSUM_OFFSET;
        $started = hrtime(true);
        for ($index = 0; $index < $options->benchRuns; $index++) {
            $checksum = self::mixUint32(
                $checksum,
                self::checksum($counter->countBytes($bytes, $options->top, $options->maxWord)),
            );
        }
        $meanMs = ((float) (hrtime(true) - $started)) / 1_000_000.0 / (float) $options->benchRuns;

        return json_encode(
            ['mean_ms' => $meanMs, 'checksum' => $checksum],
            JSON_THROW_ON_ERROR | JSON_UNESCAPED_SLASHES,
        ) . "\n";
    }

    private static function checksum(Result $result): int
    {
        $checksum = self::CHECKSUM_OFFSET;
        $checksum = self::mixUint64($checksum, $result->total);
        $checksum = self::mixUint64($checksum, $result->unique);
        foreach ($result->top as $entry) {
            $length = strlen($entry->word);
            for ($index = 0; $index < $length; $index++) {
                $checksum = self::mixByte($checksum, ord($entry->word[$index]));
            }
            $checksum = self::mixUint64($checksum, $entry->count);
        }

        return $checksum;
    }

    private static function mixByte(int $checksum, int $value): int
    {
        return (($checksum ^ $value) * self::CHECKSUM_PRIME) % self::CHECKSUM_MOD;
    }

    private static function mixUint32(int $checksum, int $value): int
    {
        for ($index = 0; $index < 4; $index++) {
            $checksum = self::mixByte($checksum, $value % 256);
            $value = intdiv($value, 256);
        }

        return $checksum;
    }

    private static function mixUint64(int $checksum, int $value): int
    {
        for ($index = 0; $index < 8; $index++) {
            $checksum = self::mixByte($checksum, $value % 256);
            $value = intdiv($value, 256);
        }

        return $checksum;
    }

    private static function write(mixed $stream, string $message): void
    {
        if (!is_resource($stream)) {
            throw new InvalidArgumentException('expected output stream resource');
        }

        fwrite($stream, $message);
    }
}
