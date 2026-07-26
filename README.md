# Word Frequency Counter

This repository solves one deliberately narrow ASCII word-frequency problem in
20 languages. Nineteen implementations are executable CLIs; the twentieth is a
pure Roc package. Clarity comes first and performance second, with each
implementation following its matching profile in `~/dev/personal/standards`.

Correctness comes from `~/dev/personal/tokenfreq-c99`. Its generated binary is
not vendored here. Set `WFC_ORACLE` only if that checkout lives somewhere else.

## Contract

Input is a raw byte stream. A word is a maximal run of `A-Z` or `a-z`; every
other byte is a separator. Words are lowercased using ASCII byte rules and
sorted by count descending, then word ascending.

Every executable accepts:

```text
--json
--top N
--top=N
--max-word N
--max-word=N
<file>
```

`--top` must be positive. `--max-word=0` selects the oracle default of 64; all
other nonnegative values clamp to `[4, 1024]`. JSON results have this shape:

```json
{
  "total": 1,
  "unique": 1,
  "top": [{ "word": "example", "count": 1 }]
}
```

## Fairness

To keep the comparison honest, every implementation reads the file once, scans
and lowercases ASCII words, counts everything, materializes every unique entry,
fully sorts the result, and truncates only afterward. Implementations use
ordinary standard containers and default hashing. C and Fortran carry small
self-contained open-addressed tables because their standard libraries do not
supply maps. Haskell keeps `Data.Map.Strict` instead of adding a hash-map
package.

Input-derived capacity hints are allowed. Custom hashers, top-N selection,
SIMD, mmap, threads, PGO, LTO-only profiles, `-march=native`, fixture shortcuts,
and benchmark-only algorithms are not.

The harness measures two different costs:

- Warm-task timing reads the fixture once, warms the counting path, validates a
  stable checksum, and reports the median and worst of five independent samples.
  Each default sample performs 3 warmups and 10 measured iterations.
- CLI timing interleaves five whole-process samples with an empty-input baseline
  and reports the median and worst sample after subtracting that startup
  baseline.

Lua is the one timing caveat: its warm path uses the standard `os.clock`, so the
adjusted CLI result is the safer wall-clock comparison.

## Implementations

| Language   | Directory     | Idiomatic shape                                                             |
| ---------- | ------------- | --------------------------------------------------------------------------- |
| C23        | `c/`          | Library and CLI with explicit ownership and a compact local hash table      |
| C++23      | `cpp/`        | `std::unordered_map`, `std::from_chars`, vectors, and ranges sorting        |
| Rust 2024  | `rust/`       | Borrowed byte scanning, ordinary `HashMap`, and explicit rendering          |
| Go         | `go/`         | Small internal package over byte slices and standard collections            |
| JavaScript | `javascript/` | Checked ESM using `Uint8Array`, `Map`, and native argument parsing          |
| TypeScript | `typescript/` | Pure counting core with Effect at file-I/O and failure boundaries           |
| PHP        | `php/`        | Strict Composer package with immutable result values and a thin bin wrapper |
| C#         | `csharp/`     | .NET console app with a focused parser and scanner accumulator              |
| Lua        | `lua/`        | Direct module and executable script over ordinary tables                    |
| Kotlin     | `kotlin/`     | Strict Gradle JVM app using byte arrays and Kotlin collections              |
| Elixir     | `elixir/`     | Mix escript and immutable byte reducer                                      |
| Zig        | `zig/`        | Native build, explicit allocator ownership, and `StringHashMap`             |
| Haskell    | `haskell/`    | Cabal app with strict `ByteString` folds and pure core functions            |
| Fortran    | `fortran/`    | fpm app, stream-byte input, modules, and a local hash table                 |
| SPARK/Ada  | `spark/`      | Proved scanner/checksum core with an Ada I/O and rendering boundary         |
| GDScript   | `gdscript/`   | Headless Godot script using packed bytes and `Dictionary`                   |
| Odin       | `odin/`       | Native CLI using the core allocator and standard map                        |
| Python     | `python/`     | Typed package-style CLI using a dictionary and byte iteration               |
| Shell      | `shell/`      | Strict Bash pipeline built from standard text tools                         |
| Roc        | `roc/`        | Pure checked package; see the non-executable exception below                |

## Toolchains

These are the language-facing toolchains used for this snapshot:

| Stack      | Snapshot toolchain                                          |
| ---------- | ----------------------------------------------------------- |
| C / C++    | Clang 22.1.8, CMake 4.3.4                                   |
| Rust       | Rust 1.97.1, cargo-deny 0.20.2                              |
| Go         | Go 1.26.5                                                   |
| JavaScript | Node 26.5.0, Bun 1.3.14, TypeScript 7.0.2 checks            |
| TypeScript | Bun 1.3.14, TypeScript 6.0.3, Effect 3.22.0                 |
| PHP        | PHP 8.5.8, Composer 2.10.2                                  |
| C#         | .NET SDK 10.0.302                                           |
| Lua        | Lua 5.5.0                                                   |
| Kotlin     | Kotlin 2.4.10, Java 26.0.2, Gradle 9.6.1                    |
| Elixir     | Elixir 1.20.2, Erlang/OTP 29.0.3                            |
| Zig        | Zig 0.16.0                                                  |
| Haskell    | GHC 9.14.1, Cabal 3.16.1.0                                  |
| Fortran    | Flang 22.1.8 semantic gate; GFortran 15.2.0 benchmark build |
| SPARK/Ada  | GNAT/GNATprove 16.1 through Alire 2.1.1                     |
| GDScript   | Godot 4.7.1, Python 3.12.13 tooling                         |
| Odin       | Odin `dev-2026-07a`                                         |
| Python     | CPython 3.14.6                                              |
| Shell      | Host GNU Bash 5.3.9 (not mise-managed)                      |
| Roc        | immutable nightly `2026-07-25-b6cdced`                      |

Every mise-managed runtime and support tool is pinned in
`.config/mise/config.toml` and `.config/mise/mise.lock`, and every resolved pin
matches the registry's latest version. The only odd-looking `mise outdated`
entry is boringlint: the Go backend records the installed tag as `v0.9.5`,
while the registry normalizes the same version to `0.9.5`, so mise reports no
available bump. Benchmark output includes the complete active mise matrix,
host, timestamp, git revision, and sampling parameters. Bash is the recorded
host-runtime exception.

Four ecosystems need special handling:

- Roc has no numbered stable compiler release. The exact immutable nightly is
  the standards profile's available reproducible choice. Its package passes
  `roc check`, but the current Roc platform boundary cannot produce a compatible
  CLI, so Roc is neither oracle-validated nor benchmarked.
- Odin publishes dated development releases rather than stable semantic
  versions; `dev-2026-07a` is the latest available release.
- The Effect-based TypeScript profile uses TypeScript 6.0.3, the latest stable
  version mutually supported by its current `typescript-eslint` and
  `@effect/language-service` stack. Moving it to TypeScript 7 requires the
  separate `@effect/tsgo` integration.
- The current mise/conda GFortran package stops at 15.2.0. Flang 22.1.8 supplies
  the latest stable Fortran semantic compilation gate, while GFortran remains
  the linked executable used for validation and timing because the conda Flang
  package lacks its link-time runtime library.

Detekt 2.0.0-alpha.5 is another upstream prerelease, but it is a support tool,
not a language runtime. The Kotlin standards profile pins it as the analyzer.

## Benchmark Corpus

The default corpus is generated deterministically under `build/fixtures`.

| Fixture         |   Bytes | Role                                   |
| --------------- | ------: | -------------------------------------- |
| `tiny-mix`      |   4,100 | Startup-sensitive smoke input          |
| `small-mix`     |  65,542 | Cache-friendly mixed input             |
| `medium-mix`    | 524,299 | Primary mixed repeated/unique workload |
| `unique-sort`   | 524,294 | Allocation and full-sort stress        |
| `case-fold-mix` | 524,295 | ASCII normalization stress             |

The `stress` corpus adds repeated-scan and long-word clamp cases.

## Benchmark Snapshot

Recorded at `2026-07-26T02:32:40.616Z` on Linux
`6.18.33.1-microsoft-standard-WSL2` x64, AMD Ryzen 9 9950X3D, 24 logical
CPUs, revision `40e5f18+dirty`. Protocol: 5 warm samples, 10 measured
iterations and 3 warmups per sample, plus 5 CLI samples. The complete toolchain
matrix emitted by the run includes all mise-managed support tools; the table
above summarizes its language-facing subset. Because the revision is dirty,
this snapshot is local worktree evidence rather than a commit-reproducible
artifact; rerun it after committing for an artifact-grade record.

| implementation | tiny median ms | small median ms | medium median ms | unique median ms | case-fold median ms | medium worst-of-5 ms | medium MB/s | adjusted CLI median ms | adjusted CLI worst-of-5 ms |
| -------------- | -------------: | --------------: | ---------------: | ---------------: | ------------------: | -------------------: | ----------: | ---------------------: | -------------------------: |
| rust           |          0.012 |           0.202 |            1.773 |            1.650 |               1.888 |                1.872 |       282.1 |                  2.583 |                      2.723 |
| zig            |          0.010 |           0.295 |            2.733 |            3.782 |               2.337 |                2.734 |       182.9 |                  3.532 |                      4.054 |
| cpp            |          0.014 |           0.301 |            2.763 |            4.335 |               2.318 |                2.783 |       181.0 |                  3.720 |                      3.827 |
| c              |          0.014 |           0.305 |            2.857 |            4.685 |               2.517 |                2.941 |       175.0 |                  3.952 |                      4.205 |
| go             |          0.025 |           0.441 |            3.936 |            4.346 |               3.292 |                4.012 |       127.0 |                  4.187 |                      4.630 |
| fortran        |          0.026 |           0.498 |            4.670 |           10.346 |               5.207 |                4.691 |       107.1 |                  6.429 |                      6.602 |
| kotlin         |          0.495 |           1.640 |            5.416 |            6.430 |               4.894 |                5.725 |        92.3 |                 43.921 |                     46.650 |
| odin           |          0.032 |           0.619 |            5.694 |            8.971 |               4.501 |                5.726 |        87.8 |                  6.929 |                      7.191 |
| csharp         |          0.081 |           1.259 |            7.927 |            7.167 |               6.126 |                7.970 |        63.1 |                 17.022 |                     18.522 |
| haskell        |          0.127 |           0.883 |            9.236 |            9.777 |              10.067 |                9.267 |        54.1 |                 13.318 |                     13.848 |
| javascript     |          0.177 |           1.382 |           11.655 |           12.959 |               9.749 |               11.715 |        42.9 |                 20.484 |                     21.841 |
| typescript     |          0.307 |           2.695 |           13.494 |           13.694 |              12.057 |               14.080 |        37.1 |                 47.649 |                     69.991 |
| spark          |          0.121 |           1.956 |           16.730 |           19.981 |              14.222 |               16.896 |        29.9 |                 18.505 |                     20.325 |
| php            |          0.161 |           2.804 |           25.157 |           31.274 |              22.846 |               25.489 |        19.9 |                 26.860 |                     28.236 |
| python         |          0.219 |           3.498 |           29.532 |           23.420 |              24.135 |               30.319 |        16.9 |                 31.334 |                     32.718 |
| lua            |          0.404 |           6.482 |           53.950 |           55.437 |              50.446 |               56.869 |         9.3 |                 58.816 |                     59.541 |
| gdscript       |          0.407 |           7.466 |           66.326 |           94.971 |              59.070 |               66.990 |         7.5 |                102.694 |                    103.151 |
| elixir         |          0.173 |           2.710 |           71.706 |           68.159 |              60.266 |               72.111 |         7.0 |                 71.667 |                     73.847 |
| shell          |         15.915 |         203.272 |         1706.704 |         1930.415 |            1654.128 |             1719.218 |         0.3 |               1828.373 |                   1850.610 |

Use the warm medians for the primary scanner comparison. The adjusted CLI
columns show whole-command wall time after subtracting the empty-input process
baseline.

## Commands

Mise is the supported developer interface:

```sh
mise run tasks
mise run install
mise run fmt
mise run fmt:check
mise run lint
mise run build
mise run validate
mise run bench
mise run check:local
mise run check
mise run clean
```

Useful benchmark overrides include `--corpus=stress`, `--fixture=PATH`,
`--runs=N`, `--warmups=N`, `--warm-task-samples=N`, `--warm-task-runs=N`, and
`--warm-task-warmups=N`.

There are no separate per-language test suites or CI-only tasks. Instead,
`mise run validate` checks all 19 executable CLIs against `tokenfreq-c99` over
the benchmark corpus and generated edge cases, including separated and
equals-style options and max-word clamps.

## Standards Surface

`mise run fmt:check` exercises every formatter. `mise run lint` applies strict
compiler warnings and the repository profiles for Clang, Clippy with
`cargo deny`, Go module verification with
boringlint/golangci-lint/govulncheck, ESLint/TypeScript/Effect diagnostics,
Composer/PHPCS/PHPStan/PHPMD/Rector, .NET analyzers, StyLua, Detekt,
Credo/Dialyzer/audits, Zig checks, HLint/GHC/Cabal, Findent/Fortls and both
Fortran compilers, ShellCheck, Python type/security/audit tools, Godot linting,
Odin vetting, Roc checking, and SPARK proof.

Generated binaries, dependencies, caches, and reports remain ignored and are
removed by `mise run clean`.
