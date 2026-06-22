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

Rust, C++, and Zig use their standard hash maps with normal per-map default
hashers. C is the one native exception: ISO C has no standard hash map, so it
may keep a small self-contained table, but that table is treated as C's local
stand-in for the standard maps rather than a license for specialized benchmark
machinery.

Modest capacity hints are allowed when they are shared and input-derived:
implementations with standard capacity APIs may estimate unique words from input
length, reserve the current-word buffer up to `min(max_word, 64)`, and reserve
the final entry list once the unique count is known. Avoid custom hashers,
alternate hash-map libraries, top-N heaps or selection, SIMD, mmap, threads,
LTO-only benchmark profiles, PGO, `-march=native`, release-profile tweaks that
are not mirrored across comparable compiled languages, or fixture-specific
shortcuts.

The harness times built release artifacts when a language normally produces one,
uses the same `--top` and `--max-word` for every timing, and sorts the summary by
the primary corpus fixture's warm-task mean: an already-running, in-process proxy
where the fixture is read once, the counting function is warmed, and repeated
count batches are timed inside the language runtime. The warm-task checksum is a
stable FNV-1a-style rolling hash over `total`, `unique`, each top word's bytes,
and each top count; repeated timed runs are mixed into a non-canceling aggregate
that the harness validates for every timing sample. Corpus summaries print
warm-task means for each fixture and the primary fixture's adjusted CLI time.
Single-file summaries also show raw CLI and startup timings because
runtime-heavy languages can spend most of their wall time before the scanner
does meaningful work.

The per-language CLIs also accept internal `--bench-runs N` and
`--bench-warmups N` flags. They are intentionally omitted from the public
contract above; they exist only so `scripts/bench.ts` can measure the warm-task
case without adding public wrapper scripts or per-language test surfaces.

Two current caveats are explicit policy choices. Haskell keeps
`Data.Map.Strict` as a standard-library caveat instead of adding
`unordered-containers`. Lua's warm-task timer uses standard Lua's `os.clock`, so
that column is CPU-time-based for Lua; use its adjusted CLI timing for the safer
cross-language wall-time comparison.

## Implementations

| Language   | Shape                                  | Commentary                                                                                                                                                                                                                                            |
| ---------- | -------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| C23        | `c/` library plus CLI                  | The native exception for C's missing standard map: byte scanner, small open-addressed table, explicit allocation and cleanup.                                                                                                                         |
| C++26      | single CLI                             | Modern standard-library version: `std::from_chars`, `std::unordered_map`, vectors, and ranges sorting without turning the solution into a framework.                                                                                                  |
| Rust 2024  | library plus CLI                       | Ownership-conscious core over `&[u8]`, ordinary `HashMap`, borrowed `Cow<[u8]>` keys for already-lowercase words, byte-backed result entries, and explicit render functions. The `case-fold-mix` fixture keeps that representation advantage visible. |
| Go         | `internal/wordcount` plus command      | Reads bytes with `io.ReadAll`, scans directly, and keeps the package boundary natural Go. The code stays deliberately boring.                                                                                                                         |
| JavaScript | ESM CLI with `checkJs`                 | Uses `Uint8Array`, `Map`, and explicit ASCII helpers. The implementation is readable, with string accumulation still visible once startup is removed from the benchmark.                                                                              |
| PHP        | Composer package plus thin bin wrapper | The most standards-heavy dynamic implementation: strict types, value objects, PHPCS, PHPMD, PHPStan, Psalm, Deptrac, and Rector dry-run. The code is more formal because the quality gate is more formal.                                             |
| C#         | .NET console app                       | Reads bytes with `File.ReadAllBytes`, carries scanner state in an accumulator, and benchmarks the built Release app directly.                                                                                                                         |
| Lua        | module plus executable script          | Compact table-based scanner with a small CLI wrapper. It is a good example of Lua being direct without pretending to be a static language.                                                                                                            |
| Kotlin     | Gradle JVM app                         | Uses a byte array, `StringBuilder`, unsigned counts, and locked Gradle tooling. The warm-task columns compare scanner work, while the adjusted CLI column keeps JVM startup cost visible.                                                             |
| Elixir     | Mix escript                            | Builds a prod escript and expresses the scanner as a reducer over bytes with immutable maps. It is elegant BEAM code for the problem, not a claim that BEAM is ideal for tiny byte-counting CLIs.                                                     |
| Zig        | single native CLI                      | Explicit allocator ownership, scoped arena lifetime, `StringHashMap`, and low ceremony. It makes the byte-level mechanics visible without C's manual cleanup surface.                                                                                 |
| Haskell    | GHC-built CLI                          | Strict `ByteString` fold into `Data.Map.Strict`, with pure parse/render/counting pieces. `Map` is a deliberate standard-library caveat rather than an extra dependency for a hash table.                                                              |

## Benchmark Corpus

The default benchmark is a deterministic generated corpus, not a single
representative text file. That keeps the table honest about size and
cardinality effects without committing generated fixtures to git.

| Fixture         |    Size | Role    | What it exposes                                      |
| --------------- | ------: | ------- | ---------------------------------------------------- |
| `tiny-mix`      |   4 KiB | smoke   | Startup noise and fixed overheads                    |
| `small-mix`     |  64 KiB | cache   | Scanner and map behavior on a still-small input      |
| `medium-mix`    | 512 KiB | primary | Mixed repeated and unique words; headline sort order |
| `unique-sort`   | 512 KiB | stress  | Mostly unique words, allocation pressure, full sort  |
| `case-fold-mix` | 512 KiB | stress  | Uppercase-heavy repeats and ASCII normalization cost |

The `stress` corpus adds `repeated-scan` and `long-clamp` for low-cardinality
throughput and max-word truncation pressure. Use `--fixture=...` when you want a
one-off file instead of the generated corpus.

## Benchmark Snapshot

Short local corpus sanity run:

```sh
mise run bench -- --runs=2 --warmups=1 --warm-task-samples=1 --warm-task-runs=10 --warm-task-warmups=3
```

| implementation | tiny-mix warm ms | small-mix warm ms | medium-mix warm ms | unique-sort warm ms | case-fold-mix warm ms | medium-mix MB/s | medium-mix adjusted CLI ms |
| -------------- | ---------------: | ----------------: | -----------------: | ------------------: | --------------------: | --------------: | -------------------------: |
| rust           |            0.012 |             0.209 |              1.841 |               1.711 |                 1.876 |           271.6 |                      2.320 |
| zig            |            0.009 |             0.268 |              2.687 |               3.742 |                 2.315 |           186.1 |                      3.164 |
| cpp            |            0.014 |             0.303 |              2.822 |               4.354 |                 2.386 |           177.2 |                      3.635 |
| c              |            0.014 |             0.302 |              3.005 |               4.617 |                 2.500 |           166.4 |                      4.066 |
| go             |            0.026 |             0.499 |              4.545 |               5.107 |                 3.797 |           110.0 |                      5.583 |
| kotlin         |            0.503 |             1.562 |              5.743 |               6.362 |                 4.988 |            87.1 |                     41.964 |
| csharp         |            0.085 |             1.330 |              8.021 |               7.346 |                 6.245 |            62.3 |                     16.492 |
| haskell        |            0.123 |             0.911 |              9.370 |              10.167 |                10.311 |            53.4 |                     12.862 |
| javascript     |            0.177 |             1.528 |             12.045 |              13.140 |                10.459 |            41.5 |                     22.311 |
| php            |            0.256 |             4.448 |             37.603 |              43.949 |                35.608 |            13.3 |                     38.575 |
| lua            |            0.653 |             7.976 |             63.833 |              73.355 |                61.420 |             7.8 |                     67.433 |
| elixir         |            0.176 |             2.701 |             71.602 |              68.709 |                60.818 |             7.0 |                     60.644 |

Interpret the `warm ms` columns as the closest benchmark here to an
already-running service or application doing this task in the middle of a larger
workload. They do not include process startup or file reads. The `medium-mix
MB/s` column is derived from the `medium-mix warm ms` value so scaling is easier
to read. Interpret `adjusted CLI ms` as whole-command timing after subtracting
the empty-fixture command baseline, not as the primary in-process result. The
harness builds first, validates every benchmark fixture and generated edge-case
fixture against `tokenfreq-c99`, runs warmups for every implementation, then
times interleaved samples of each benchmark fixture and an empty-fixture
invocation with the same command options.

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
mise run check
mise run clean
```

Useful benchmark options:

```sh
mise run bench -- --corpus=default --top=10 --max-word=1024 --runs=5 --warmups=3 --warm-task-samples=3 --warm-task-runs=50 --warm-task-warmups=10
mise run bench -- --corpus=stress --runs=3 --warm-task-runs=20
mise run bench -- --fixture=fixtures/spec.txt --runs=5
mise run validate
```

The default `mise run bench` corpus is generated under
`build/fixtures/bench-*.txt`. For compatibility, the primary corpus fixture is
also written to `build/fixtures/benchmark.txt`. Use `--fixture=fixtures/spec.txt`
when you want the tiny contract fixture instead of the corpus.

The mise environment sets `WFC_CORPUS=default`. `WFC_TOP`, `WFC_MAX_WORD`, and
`WFC_ORACLE` remain environment-level defaults for local runs. `WFC_FIXTURE` is
still accepted as a legacy custom-fixture default, but only when `WFC_CORPUS` is
unset; explicit `--fixture=...` and `--corpus=...` flags are clearer for new
runs.

There are intentionally no per-language test suites. Correctness is the oracle
comparison: `mise run validate` checks every benchmark fixture, or the supplied
custom fixture, plus a small matrix of generated edge-case fixtures against
`tokenfreq-c99`. That matrix includes separated and equals CLI flag forms for
`--top` and `--max-word`, including max-word clamp cases. There is also
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
