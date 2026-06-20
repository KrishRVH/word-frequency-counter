# Word Frequency Counter

One ASCII word-frequency problem implemented across twelve languages. The point
is not to chase every last cycle; it is to compare small, idiomatic,
well-policed implementations that all obey the same byte-level contract.

The implementations are meant to be good examples of each language, not
translations forced through one architecture. Prefer the smallest clear shape
that feels natural in that language. The shared harness carries the edge-case
correctness pressure so the language directories can stay readable and useful
as reference code.

The correctness source of truth is
`~/dev/personal/tokenfreq-c99`. This repository builds or invokes that C99
oracle and compares every implementation against its JSON output.

The benchmark only needs the oracle executable, but the binary is intentionally
not vendored here. It is generated, platform-specific, and easy to let drift
from `tokenfreq-c99`; keep the source repo as the authority and override the
binary path with `WFC_ORACLE` when needed.

## Contract

Input is a raw byte stream. A word is a maximal run of ASCII letters:

```text
A-Z or a-z
```

All other bytes are separators: whitespace, punctuation, digits, NUL bytes, and
non-ASCII bytes. Words are lowercased with ASCII byte logic only. Results are
sorted by count descending, then word ascending.

Every implementation accepts:

```sh
--json
--top N
--top=N
--max-word N
--max-word=N
<file>
```

`--top` must be greater than zero. `--max-word` follows the oracle: `0` means
the oracle default of `64`, and every other nonnegative value clamps into
`[4, 1024]`.

The local JSON shape is:

```json
{
  "total": 1,
  "unique": 1,
  "top": [{ "word": "example", "count": 1 }]
}
```

The oracle emits a slightly different shape, so `scripts/bench.ts` normalizes
`tokenfreq-c99` before comparison.

## Fairness Rules

The benchmark is a comparison of clear, idiomatic implementations, not a contest
to smuggle in each language's most specialized data structure. The shared shape
is:

1. Read the input as bytes.
2. Scan ASCII words and lowercase with byte logic.
3. Count every word.
4. Materialize every unique entry.
5. Fully sort by count descending, then word ascending.
6. Truncate only after sorting.

Rust, C++, and Zig use their standard hash maps with default hashers. C is the
one native exception: ISO C has no standard hash map, so it may keep a small
self-contained table, but that table is treated as C's local stand-in for the
standard maps rather than a license for specialized benchmark machinery.

Modest capacity hints are allowed when they are shared and input-derived:
implementations with standard capacity APIs may estimate unique words from input
length, reserve the current-word buffer up to `min(max_word, 64)`, and reserve
the final entry list once the unique count is known. Avoid custom hashers,
alternate hash-map libraries, top-N heaps or selection, SIMD, mmap, threads,
LTO-only benchmark profiles, PGO, `-march=native`, or fixture-specific
shortcuts.

The harness times built release artifacts when a language normally produces one,
uses the same `--top` and `--max-word` for the startup baseline, and reports raw,
startup, adjusted mean, and adjusted p95 timings. That keeps runtime startup
visible instead of pretending it vanished.

## Implementations

| Language   | Shape                                  | Commentary                                                                                                                                                                                                |
| ---------- | -------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| C23        | `c/` library plus CLI                  | The native exception for C's missing standard map: byte scanner, small open-addressed table, explicit allocation and cleanup.                                                                             |
| C++26      | single CLI                             | Modern standard-library version: `std::from_chars`, `std::unordered_map`, vectors, and ranges sorting without turning the solution into a framework.                                                      |
| Rust 2024  | library plus CLI                       | Small ownership-conscious core over `&[u8]`, `HashMap`, and explicit render functions. It is one of the cleanest balances of safety, speed, and readability here.                                         |
| Go         | `internal/wordcount` plus command      | Reads bytes with `io.ReadAll`, scans directly, and keeps the package boundary natural Go. The code stays deliberately boring.                                                                             |
| JavaScript | ESM CLI with `checkJs`                 | Uses `Uint8Array`, `Map`, and explicit ASCII helpers. The implementation is readable, with string accumulation still visible once startup is removed from the benchmark.                                  |
| PHP        | Composer package plus thin bin wrapper | The most standards-heavy dynamic implementation: strict types, value objects, PHPCS, PHPMD, PHPStan, Psalm, Deptrac, and Rector dry-run. The code is more formal because the quality gate is more formal. |
| C#         | .NET console app                       | Reads bytes with `File.ReadAllBytes`, carries scanner state in an accumulator, and benchmarks the built Release app directly.                                                                             |
| Lua        | module plus executable script          | Compact table-based scanner with a small CLI wrapper. It is a good example of Lua being direct without pretending to be a static language.                                                                |
| Kotlin     | Gradle JVM app                         | Uses a byte array, `StringBuilder`, unsigned counts, and locked Gradle tooling. The implementation is clear, and the benchmark subtracts the JVM command startup baseline.                                |
| Elixir     | Mix escript                            | Expresses the scanner as a reducer over bytes with immutable maps. It is elegant BEAM code for the problem, not a claim that BEAM is ideal for tiny byte-counting CLIs.                                   |
| Zig        | single native CLI                      | Explicit allocator ownership, scoped arena lifetime, `StringHashMap`, and low ceremony. It makes the byte-level mechanics visible without C's manual cleanup surface.                                     |
| Haskell    | GHC-built CLI                          | Strict `ByteString` fold into `Data.Map.Strict`, with pure parse/render/counting pieces. `Map` is a deliberate standard-library caveat rather than an extra dependency for a hash table.                  |

## Benchmark Snapshot

Last local run:

```sh
mise run bench -- --runs=10 --warmups=5
```

| implementation | status | raw mean ms | startup mean ms | adjusted mean ms | adjusted p95 ms |
| -------------- | -----: | ----------: | --------------: | ---------------: | --------------: |
| rust           |     ok |       2.295 |           0.748 |            1.546 |           1.711 |
| c              |     ok |       2.524 |           0.710 |            1.813 |           2.057 |
| zig            |     ok |       2.604 |           0.750 |            1.854 |           2.176 |
| cpp            |     ok |       3.022 |           1.135 |            1.886 |           2.067 |
| go             |     ok |       4.148 |           1.405 |            2.743 |           3.242 |
| haskell        |     ok |       7.235 |           1.355 |            5.880 |           6.229 |
| csharp         |     ok |      53.541 |          42.618 |           10.923 |          12.165 |
| elixir         |     ok |     253.144 |         242.038 |           11.106 |          21.506 |
| javascript     |     ok |      33.081 |          18.166 |           14.914 |          16.905 |
| php            |     ok |      34.639 |          10.963 |           23.677 |          24.718 |
| kotlin         |     ok |      95.793 |          60.559 |           35.233 |          40.514 |
| lua            |     ok |      39.592 |           1.242 |           38.351 |          39.783 |

Interpret `adjusted mean ms` as the implementation timing after subtracting the
empty-fixture command baseline, not as whole-command time. The harness builds
first, validates the requested fixture and generated edge-case fixtures against
`tokenfreq-c99`, runs warmups for every implementation, then times interleaved
samples of both the requested fixture and an empty-fixture invocation with the
same command options. The raw and startup columns are kept visible because
runtime-heavy languages can spend most of their wall time before the scanner does
meaningful work.

## Commands

Everything public goes through `mise`:

```sh
mise run tasks
mise run install
mise run fmt
mise run fmt:check
mise run lint
mise run build
mise run validate
mise run bench -- --runs=10
mise run check:local
mise run clean
```

Useful benchmark options:

```sh
mise run bench -- --fixture=fixtures/spec.txt --top=10 --max-word=1024 --runs=5 --warmups=3
mise run validate
```

The default `mise run bench` fixture is generated at
`build/fixtures/benchmark.txt` with repeated words, unique words, and mixed
ASCII separators; use `--fixture=fixtures/spec.txt` when you want the tiny
contract fixture instead.

There are intentionally no per-language test suites. Correctness is the oracle
comparison: `mise run validate` checks the requested fixture and a small matrix
of generated edge-case fixtures against `tokenfreq-c99`. There is also
intentionally no CI, no Dagger setup, and no GitHub workflow surface.

## Standards Surface

The repository is strict by design:

| Stack      | Gate                                                                                      |
| ---------- | ----------------------------------------------------------------------------------------- |
| C / C++    | CMake, Clang warnings as errors, sanitizers in debug preset, `clang-format`, `clang-tidy` |
| Rust       | `cargo fmt`, Clippy with warnings denied                                                  |
| Go         | `gofumpt`, `go vet`, `golangci-lint`, `govulncheck`                                       |
| JavaScript | Prettier, ESLint, TypeScript `checkJs`, Node syntax check                                 |
| PHP        | Composer Normalize, PHPCS, PHPMD, PHPStan, Psalm, Deptrac, Rector dry-run                 |
| C#         | `dotnet format`, warnings as errors                                                       |
| Lua        | StyLua, Luacheck, Lua Language Server diagnostics                                         |
| Kotlin     | ktlint, Detekt, Gradle dependency locking and verification metadata                       |
| Elixir     | `mix format`, Credo strict mode, warnings as errors                                       |
| Haskell    | Ormolu, HLint, GHC `-Wall -Werror`, Cabal check/build                                     |
| Zig        | `zig fmt`, compile check                                                                  |
| Benchmark  | Bun/TypeScript runner validates every implementation against the oracle                   |

## Layout

```text
c/            C23 library and CLI
cpp/          C++26 CLI
rust/         Rust crate
go/           Go module
javascript/   Node ESM implementation
php/          Composer package and executable wrapper
csharp/       .NET console app
lua/          Lua module and script
kotlin/       Gradle JVM app
elixir/       Mix escript
zig/          Zig CLI
haskell/      GHC/Cabal app
scripts/      Bun benchmark and oracle-validation runner
fixtures/     small checked-in fixture
```

Generated binaries, dependencies, caches, and reports are ignored. GitHub
Linguist is configured to count maintained implementation source instead of
docs, lockfiles, fixtures, vendored dependencies, and build output.
