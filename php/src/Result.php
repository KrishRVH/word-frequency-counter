<?php

declare(strict_types=1);

namespace WordFrequencyCounter;

final readonly class Result
{
    /**
     * @param list<Entry> $top
     */
    public function __construct(
        public int $total,
        public int $unique,
        public array $top,
    ) {
    }
}
