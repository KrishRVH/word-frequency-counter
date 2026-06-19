<?php

declare(strict_types=1);

namespace WordFrequencyCounter;

final readonly class CliOptions
{
    public function __construct(
        public string $path,
        public int $top,
        public int $maxWord,
        public bool $json,
    ) {
    }
}
