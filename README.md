# Word Frequency Counter

One ASCII word-frequency problem implemented across twelve languages. The point
is not to chase every last cycle; it is to compare small, idiomatic,
well-policed implementations that all obey the same byte-level contract.

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

## Implementations

| Language   | Shape                                  | Commentary                                                                                                                                                                                                |
| ---------- | -------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| C23        | `c/` library plus CLI                  | The closest implementation to the oracle style: byte scanner, open-addressed hash table, explicit allocation and cleanup. It is the systems baseline for the repo.                                        |
| C++26      | single CLI                             | Modern standard-library version: `std::from_chars`, `std::unordered_map`, vectors, and ranges sorting without turning the solution into a framework.                                                      |
| Rust 2024  | library plus CLI                       | Small ownership-conscious core over `&[u8]`, `HashMap`, and explicit render functions. It is one of the cleanest balances of safety, speed, and readability here.                                         |
| Go         | `internal/wordcount` plus command      | Streams through a buffered reader instead of slurping the file. The package boundary is natural Go, and the code stays deliberately boring.                                                               |
| JavaScript | ESM CLI with `checkJs`                 | Uses `Uint8Array`, `Map`, and explicit ASCII helpers. The implementation is readable, but string accumulation and Node startup are visible in the benchmark.                                              |
| PHP        | Composer package plus thin bin wrapper | The most standards-heavy dynamic implementation: strict types, value objects, PHPCS, PHPMD, PHPStan, Psalm, Deptrac, and Rector dry-run. The code is more formal because the quality gate is more formal. |
| C#         | .NET console app                       | Streams through `FileStream` in chunks and carries scanner state in an accumulator. The core is sensible C#, but this benchmark includes `dotnet run --no-build` startup cost.                            |
| Lua        | module plus executable script          | Compact table-based scanner with a small CLI wrapper. It is a good example of Lua being direct without pretending to be a static language.                                                                |
| Kotlin     | Gradle JVM app                         | Uses a byte array, `StringBuilder`, unsigned counts, and locked Gradle tooling. The implementation is clear, while JVM process startup dominates the small fixture timing.                                |
| Elixir     | Mix escript                            | Expresses the scanner as a reducer over bytes with immutable maps. It is elegant BEAM code for the problem, not a claim that BEAM is ideal for tiny byte-counting CLIs.                                   |
| Zig        | single native CLI                      | Explicit allocator ownership, arena lifetime, `StringHashMap`, and low ceremony. Zig ended up as the final advisory language because it makes the byte-level mechanics visible without C's footguns.      |
| Haskell    | GHC-built CLI                          | Strict `ByteString` fold into `Data.Map.Strict`, with pure parse/render/counting pieces. It is surprisingly competitive for a high-level implementation once compiled native.                             |

## Benchmark Snapshot

Last local run:

```sh
mise run bench -- --runs=3
```

| Implementation | Status | Mean ms |
| -------------- | -----: | ------: |
| C              |     ok |   0.574 |
| Zig            |     ok |   0.602 |
| Rust           |     ok |   0.610 |
| Lua            |     ok |   0.863 |
| C++            |     ok |   0.967 |
| Haskell        |     ok |   1.097 |
| Go             |     ok |   1.602 |
| PHP            |     ok |   8.605 |
| JavaScript     |     ok |  15.905 |
| Kotlin         |     ok |  57.377 |
| Elixir         |     ok | 221.132 |
| C#             |     ok | 388.563 |

Interpret these numbers as whole-command timings, not as pure inner-loop
microbenchmarks. The harness builds first, validates against `tokenfreq-c99`,
then times each implementation by spawning its CLI. That makes startup cost part
of the result, which is why native binaries cluster tightly and JVM, BEAM, and
.NET look much slower on the small fixture.

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
mise run bench -- --fixture=fixtures/spec.txt --top=10 --max-word=1024 --runs=5
mise run validate
```

There are intentionally no per-language test suites. Correctness is the oracle
comparison. There is also intentionally no CI, no Dagger setup, and no GitHub
workflow surface.

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
