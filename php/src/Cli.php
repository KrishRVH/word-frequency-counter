<?php

declare(strict_types=1);

namespace WordFrequencyCounter;

use InvalidArgumentException;
use Throwable;

final class Cli
{
    private const string USAGE = "usage: php/bin/wordcount [--json] [--top N] [--max-word N] <file>\n";

    /**
     * @param list<string> $arguments
     */
    public static function main(array $arguments, mixed $stdout, mixed $stderr): int
    {
        try {
            $options = self::parse($arguments);
            $result = (new WordCounter())->countFile($options->path, $options->top, $options->maxWord);
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
        $top = 10;
        $maxWord = 1024;
        $path = null;
        $argumentCount = count($arguments);

        for ($index = 1; $index < $argumentCount; $index++) {
            $argument = $arguments[$index];

            if ($argument === '--json') {
                $json = true;
                continue;
            }

            $topOption = self::readNumberOption($arguments, $argument, '--top', '--top=', $index);
            if ($topOption !== null) {
                $index = $topOption['index'];
                $top = $topOption['value'];
                continue;
            }

            $maxWordOption = self::readNumberOption(
                $arguments,
                $argument,
                '--max-word',
                '--max-word=',
                $index,
            );
            if ($maxWordOption !== null) {
                $index = $maxWordOption['index'];
                $maxWord = $maxWordOption['value'];
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

        if ($path === null || $top <= 0) {
            throw new InvalidArgumentException('invalid options');
        }

        return new CliOptions($path, $top, $maxWord, $json);
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

    private static function write(mixed $stream, string $message): void
    {
        if (!is_resource($stream)) {
            throw new InvalidArgumentException('expected output stream resource');
        }

        fwrite($stream, $message);
    }
}
