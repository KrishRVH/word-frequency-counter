<?php

declare(strict_types=1);

namespace WordFrequencyCounter;

final class WordCounter
{
    private const int ORACLE_DEFAULT_MAX_WORD = 64;
    private const int MAX_WORD_LIMIT = 1024;
    private const int MIN_WORD = 4;

    public static function countBytes(string $bytes, int $top, int $maxWord): Result
    {
        $maxWord = self::normalizeMaxWord($maxWord);

        /** @phpstan-var array<string, int> $counts */
        $counts = [];
        $word = '';
        $total = 0;
        $length = strlen($bytes);

        for ($index = 0; $index < $length; $index++) {
            $lower = ord($bytes[$index]) | 32;

            if ($lower >= 97 && $lower <= 122) {
                if (strlen($word) < $maxWord) {
                    $word .= chr($lower);
                }
                continue;
            }

            if ($word !== '') {
                $counts[$word] = ($counts[$word] ?? 0) + 1;
                $total++;
                $word = '';
            }
        }

        if ($word !== '') {
            $counts[$word] = ($counts[$word] ?? 0) + 1;
            $total++;
        }

        $entries = [];
        foreach ($counts as $entryWord => $count) {
            $entries[] = new Entry($entryWord, $count);
        }

        usort(
            $entries,
            static function (Entry $left, Entry $right): int {
                $countOrder = $right->count <=> $left->count;
                if ($countOrder !== 0) {
                    return $countOrder;
                }

                return strcmp($left->word, $right->word);
            },
        );

        return new Result($total, count($counts), array_slice($entries, 0, $top));
    }

    private static function normalizeMaxWord(int $value): int
    {
        if ($value === 0) {
            return self::ORACLE_DEFAULT_MAX_WORD;
        }

        return min(max($value, self::MIN_WORD), self::MAX_WORD_LIMIT);
    }
}
