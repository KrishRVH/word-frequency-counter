<?php

declare(strict_types=1);

namespace WordFrequencyCounter;

final readonly class CliOptionValue
{
    public function __construct(
        public int $index,
        public int $value,
    ) {
    }
}
