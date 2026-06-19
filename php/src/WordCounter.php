<?php

declare(strict_types=1);

namespace WordFrequencyCounter;

use RuntimeException;

final class WordCounter
{
    public function countFile(string $path, int $top, int $maxWord): Result
    {
        if (!is_file($path) || !is_readable($path)) {
            throw new RuntimeException("cannot read {$path}");
        }

        $bytes = file_get_contents($path);
        if ($bytes === false) {
            throw new RuntimeException("cannot read {$path}");
        }

        return $this->countBytes($bytes, $top, $maxWord);
    }

    public function countBytes(string $bytes, int $top, int $maxWord): Result
    {
        $counts = [];
        $word = '';
        $inWord = false;
        $stored = 0;
        $total = 0;
        $length = strlen($bytes);

        for ($index = 0; $index < $length; $index++) {
            $byte = ord($bytes[$index]);

            if ($this->isLetter($byte)) {
                $inWord = true;
                if ($stored < $maxWord) {
                    $word .= chr($this->lowerAscii($byte));
                    $stored++;
                }
                continue;
            }

            if ($inWord) {
                $counts[$word] = ($counts[$word] ?? 0) + 1;
                $total++;
                $word = '';
                $stored = 0;
                $inWord = false;
            }
        }

        if ($inWord) {
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

    /**
     * @param int<0, 255> $byte
     */
    private function isLetter(int $byte): bool
    {
        $lower = $byte | 32;
        return $lower >= 97 && $lower <= 122;
    }

    /**
     * @param int<0, 255> $byte
     *
     * @return int<0, 255>
     */
    private function lowerAscii(int $byte): int
    {
        return $byte >= 65 && $byte <= 90 ? $byte + 32 : $byte;
    }
}
