<?php

declare(strict_types=1);

namespace WordFrequencyCounter;

final readonly class Entry
{
    public function __construct(
        public string $word,
        public int $count,
    ) {
    }
}
