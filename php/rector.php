<?php

declare(strict_types=1);

use Rector\Config\RectorConfig;
use Rector\CodingStyle\Rector\FuncCall\FunctionFirstClassCallableRector;
use Rector\Php84\Rector\MethodCall\NewMethodCallWithoutParenthesesRector;
use Rector\Set\ValueObject\LevelSetList;
use Rector\Set\ValueObject\SetList;

return RectorConfig::configure()
    ->withoutParallel()
    ->withPaths([__DIR__ . '/src', __DIR__ . '/bin/wordcount'])
    ->withSets([
        LevelSetList::UP_TO_PHP_85,
        SetList::CODE_QUALITY,
        SetList::DEAD_CODE,
        SetList::EARLY_RETURN,
        SetList::TYPE_DECLARATION,
        SetList::PRIVATIZATION,
    ])
    ->withSkip([
        FunctionFirstClassCallableRector::class,
        NewMethodCallWithoutParenthesesRector::class,
    ])
    ->withImportNames(removeUnusedImports: true)
    ->withCache(__DIR__ . '/.rector-cache');
