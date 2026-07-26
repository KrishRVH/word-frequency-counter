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

| Language   | Directory     | Idiomatic shape                                                              |
| ---------- | ------------- | ---------------------------------------------------------------------------- |
| C23        | `c/`          | One-file borrowed scan, compact local table, and exact stable radix ordering |
| C++23      | `cpp/`        | `std::unordered_map`, `std::from_chars`, vectors, and ranges sorting         |
| Rust 2024  | `rust/`       | Borrowed byte scanning, ordinary `HashMap`, and explicit rendering           |
| Go         | `go/`         | Small internal package over byte slices and standard collections             |
| JavaScript | `javascript/` | Checked ESM using `Uint8Array`, `Map`, and native argument parsing           |
| TypeScript | `typescript/` | Pure counting core with Effect at file-I/O and failure boundaries            |
| PHP        | `php/`        | Strict Composer package with immutable result values and a thin bin wrapper  |
| C#         | `csharp/`     | .NET console app with a focused parser and scanner accumulator               |
| Lua        | `lua/`        | Direct module and executable script over ordinary tables                     |
| Kotlin     | `kotlin/`     | Strict Gradle JVM app using byte arrays and Kotlin collections               |
| Elixir     | `elixir/`     | Mix escript and immutable byte reducer                                       |
| Zig        | `zig/`        | Native build, explicit allocator ownership, and `StringHashMap`              |
| Haskell    | `haskell/`    | Cabal app with strict `ByteString` folds and pure core functions             |
| Fortran    | `fortran/`    | fpm app, stream-byte input, modules, and a local hash table                  |
| SPARK/Ada  | `spark/`      | Proved scanner/checksum core with an Ada I/O and rendering boundary          |
| GDScript   | `gdscript/`   | Headless Godot script using packed bytes and `Dictionary`                    |
| Odin       | `odin/`       | Native CLI using the core allocator and standard map                         |
| Python     | `python/`     | Typed package-style CLI using a dictionary and byte iteration                |
| Shell      | `shell/`      | Strict Bash pipeline built from standard text tools                          |
| Roc        | `roc/`        | Pure checked package; see the non-executable exception below                 |

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

Recorded at `2026-07-26T05:37:11.591Z` on Linux
`6.18.33.1-microsoft-standard-WSL2` x64, AMD Ryzen 9 9950X3D, 24 logical
CPUs, revision `d28ad26+dirty`. Protocol: 5 warm samples, 10 measured
iterations and 3 warmups per sample, plus 5 CLI samples. The complete toolchain
matrix emitted by the run includes all mise-managed support tools; the table
above summarizes its language-facing subset. Because the revision is dirty,
this snapshot is local worktree evidence rather than a commit-reproducible
artifact; rerun it after committing for an artifact-grade record.

| implementation | tiny median ms | small median ms | medium median ms | unique median ms | case-fold median ms | medium worst-of-5 ms | medium MB/s | adjusted CLI median ms | adjusted CLI worst-of-5 ms |
| -------------- | -------------: | --------------: | ---------------: | ---------------: | ------------------: | -------------------: | ----------: | ---------------------: | -------------------------: |
| c              |          0.006 |           0.072 |            0.694 |            1.131 |               0.721 |                0.708 |       720.7 |                  1.749 |                      1.900 |
| rust           |          0.012 |           0.204 |            1.790 |            1.660 |               1.862 |                1.822 |       279.3 |                  2.810 |                      3.001 |
| zig            |          0.010 |           0.304 |            2.748 |            3.787 |               2.337 |                2.773 |       182.0 |                  3.211 |                      3.434 |
| cpp            |          0.014 |           0.307 |            2.790 |            4.372 |               2.310 |                2.811 |       179.2 |                  3.670 |                      3.871 |
| go             |          0.025 |           0.442 |            4.037 |            4.335 |               3.267 |                4.135 |       123.9 |                  4.327 |                      4.484 |
| fortran        |          0.026 |           0.506 |            4.726 |           10.383 |               5.207 |                4.976 |       105.8 |                  6.506 |                      6.583 |
| kotlin         |          0.520 |           1.682 |            5.482 |            6.692 |               4.920 |                6.042 |        91.2 |                 44.137 |                     57.254 |
| odin           |          0.032 |           0.631 |            5.739 |            9.132 |               4.474 |                5.897 |        87.1 |                  6.949 |                      7.534 |
| csharp         |          0.081 |           1.285 |            8.244 |            7.413 |               6.048 |                8.794 |        60.6 |                 18.366 |                     20.435 |
| haskell        |          0.126 |           0.892 |            9.393 |            9.929 |              10.330 |               10.397 |        53.2 |                 13.438 |                     13.498 |
| javascript     |          0.171 |           1.392 |           11.610 |           13.079 |               9.627 |               12.744 |        43.1 |                 21.322 |                     25.179 |
| typescript     |          0.294 |           2.747 |           13.846 |           13.909 |              12.046 |               14.914 |        36.1 |                 48.557 |                     55.878 |
| spark          |          0.121 |           1.975 |           16.817 |           20.246 |              14.218 |               17.004 |        29.7 |                 18.547 |                     20.958 |
| php            |          0.158 |           2.843 |           26.130 |           33.657 |              23.252 |               26.509 |        19.1 |                 26.898 |                     27.152 |
| python         |          0.225 |           3.606 |           30.620 |           24.400 |              25.107 |               31.134 |        16.3 |                 32.589 |                     35.700 |
| lua            |          0.397 |           6.636 |           54.928 |           56.835 |              50.144 |               55.719 |         9.1 |                 58.533 |                     59.841 |
| gdscript       |          0.410 |           7.546 |           67.142 |           95.168 |              58.960 |               69.288 |         7.4 |                 93.081 |                    103.384 |
| elixir         |          0.172 |           2.721 |           72.658 |           68.624 |              60.007 |               73.096 |         6.9 |                 67.928 |                     83.634 |
| shell          |         16.323 |         211.859 |         1749.855 |         1971.525 |            1690.435 |             1757.114 |         0.3 |               1876.054 |                   1933.478 |

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
