import { mkdirSync, statSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { join, resolve } from "node:path";
import { performance } from "node:perf_hooks";
import { spawn } from "node:child_process";

type JsonEntry = { word: string; count: number };
type JsonResult = { total: number; unique: number; top: JsonEntry[] };

type Command = {
  cwd?: string;
  cmd: string;
  args: string[];
};

type Implementation = {
  name: string;
  build?: Command[];
  run: (fixture: string, top: number, maxWord: number) => Command;
};

type BenchOptions = {
  fixture: string;
  top: number;
  maxWord: number;
  runs: number;
  warmups: number;
  buildOnly: boolean;
  validateOnly: boolean;
};

type ValidationCase = {
  name: string;
  fixture: string;
  top: number;
  maxWord: number;
};

type BenchmarkState = {
  implementation: Implementation;
  command: Command;
  startupCommand: Command;
  totalSamples: number[];
  startupSamples: number[];
  adjustedSamples: number[];
};

type BenchmarkResult = {
  totalMeanMs: number;
  startupMeanMs: number;
  adjustedMeanMs: number;
  adjustedP95Ms: number;
};

type SummaryRow = {
  name: string;
  status: string;
  timing?: BenchmarkResult;
};

const root = resolve(import.meta.dir, "..");
const buildBin = join(root, "build", "bin");
const csharpBin = join(
  root,
  "csharp/src/WordFrequencyCounter/bin/Release/net10.0/WordFrequencyCounter" +
    (process.platform === "win32" ? ".exe" : ""),
);
const validationFixtures = join(root, "build", "fixtures");
const benchmarkFixture = join(validationFixtures, "benchmark.txt");
const startupFixture = join(validationFixtures, "startup-empty.txt");
const haskellBuild = join(root, "build", "haskell");
const tokenfreqRoot = join(homedir(), "dev/personal/tokenfreq-c99");
const defaultOracle = join(tokenfreqRoot, "build/clang/wc");
const benchmarkWords = [
  "Alpha",
  "beta",
  "GAMMA",
  "delta",
  "epsilon",
  "zeta",
  "eta",
  "theta",
];
const benchmarkSeparators = [" ", "\n", ",", "--", "123", "\t", "!!"];

const implementations: Implementation[] = [
  {
    name: "c",
    build: [
      { cmd: "cmake", args: ["--fresh", "--preset", "release"] },
      { cmd: "cmake", args: ["--build", "--preset", "release"] },
    ],
    run: (fixture, top, maxWord) => ({
      cmd: join(root, "build/c/release/wordcount_c"),
      args: [
        "--json",
        "--top",
        String(top),
        "--max-word",
        String(maxWord),
        fixture,
      ],
    }),
  },
  {
    name: "cpp",
    run: (fixture, top, maxWord) => ({
      cmd: join(root, "build/c/release/wordcount_cpp"),
      args: [
        "--json",
        "--top",
        String(top),
        "--max-word",
        String(maxWord),
        fixture,
      ],
    }),
  },
  {
    name: "rust",
    build: [
      {
        cmd: "cargo",
        args: ["build", "--release", "--manifest-path", "rust/Cargo.toml"],
      },
    ],
    run: (fixture, top, maxWord) => ({
      cmd: join(root, "rust/target/release/wordcount_rust"),
      args: [
        "--json",
        "--top",
        String(top),
        "--max-word",
        String(maxWord),
        fixture,
      ],
    }),
  },
  {
    name: "go",
    build: [
      {
        cwd: join(root, "go"),
        cmd: "go",
        args: [
          "build",
          "-trimpath",
          "-o",
          join(buildBin, "wordcount_go"),
          "./cmd/wordcount-go",
        ],
      },
    ],
    run: (fixture, top, maxWord) => ({
      cmd: join(buildBin, "wordcount_go"),
      args: [
        "--json",
        "--top",
        String(top),
        "--max-word",
        String(maxWord),
        fixture,
      ],
    }),
  },
  {
    name: "javascript",
    run: (fixture, top, maxWord) => ({
      cmd: "node",
      args: [
        "javascript/src/wordcount.js",
        "--json",
        "--top",
        String(top),
        "--max-word",
        String(maxWord),
        fixture,
      ],
    }),
  },
  {
    name: "php",
    run: (fixture, top, maxWord) => ({
      cmd: "php",
      args: [
        "php/bin/wordcount",
        "--json",
        "--top",
        String(top),
        "--max-word",
        String(maxWord),
        fixture,
      ],
    }),
  },
  {
    name: "csharp",
    build: [
      {
        cmd: "dotnet",
        args: [
          "build",
          "-c",
          "Release",
          "csharp/src/WordFrequencyCounter/WordFrequencyCounter.csproj",
        ],
      },
    ],
    run: (fixture, top, maxWord) => ({
      cmd: csharpBin,
      args: [
        "--json",
        "--top",
        String(top),
        "--max-word",
        String(maxWord),
        fixture,
      ],
    }),
  },
  {
    name: "lua",
    run: (fixture, top, maxWord) => ({
      cmd: "lua",
      args: [
        "lua/bin/wordcount.lua",
        "--json",
        "--top",
        String(top),
        "--max-word",
        String(maxWord),
        fixture,
      ],
    }),
  },
  {
    name: "kotlin",
    build: [
      {
        cwd: join(root, "kotlin"),
        cmd: "gradle",
        args: ["installDist", "--no-daemon"],
      },
    ],
    run: (fixture, top, maxWord) => ({
      cmd: join(
        root,
        "kotlin/build/install/wordcount-kotlin/bin/wordcount-kotlin",
      ),
      args: [
        "--json",
        "--top",
        String(top),
        "--max-word",
        String(maxWord),
        fixture,
      ],
    }),
  },
  {
    name: "elixir",
    build: [
      { cwd: join(root, "elixir"), cmd: "mix", args: ["deps.get"] },
      { cwd: join(root, "elixir"), cmd: "mix", args: ["escript.build"] },
    ],
    run: (fixture, top, maxWord) => ({
      cmd: join(root, "elixir/word_count"),
      args: [
        "--json",
        "--top",
        String(top),
        "--max-word",
        String(maxWord),
        fixture,
      ],
    }),
  },
  {
    name: "zig",
    build: [
      {
        cmd: "zig",
        args: [
          "build-exe",
          "-O",
          "ReleaseFast",
          "-femit-bin=build/bin/wordcount_zig",
          "zig/src/main.zig",
        ],
      },
    ],
    run: (fixture, top, maxWord) => ({
      cmd: join(buildBin, "wordcount_zig"),
      args: [
        "--json",
        "--top",
        String(top),
        "--max-word",
        String(maxWord),
        fixture,
      ],
    }),
  },
  {
    name: "haskell",
    build: [
      {
        cmd: "ghcup",
        args: [
          "run",
          "--install",
          "--ghc",
          "9.14.1",
          "--",
          "ghc",
          "-O2",
          "-Wall",
          "-Werror",
          "-outputdir",
          haskellBuild,
          "-i" + join(root, "haskell/app"),
          "-o",
          join(buildBin, "wordcount_haskell"),
          "haskell/app/Main.hs",
        ],
      },
    ],
    run: (fixture, top, maxWord) => ({
      cmd: join(buildBin, "wordcount_haskell"),
      args: [
        "--json",
        "--top",
        String(top),
        "--max-word",
        String(maxWord),
        fixture,
      ],
    }),
  },
];

const options = parseArgs(process.argv.slice(2));
mkdirSync(buildBin, { recursive: true });
mkdirSync(validationFixtures, { recursive: true });
mkdirSync(haskellBuild, { recursive: true });
writeFileSync(benchmarkFixture, createBenchmarkFixture());
writeFileSync(startupFixture, "");

await ensureOracle();
await buildAll();

if (!options.buildOnly) {
  const validationCases = createValidationCases(options);
  const expectedCases: { testCase: ValidationCase; oracle: JsonResult }[] = [];
  const rows: SummaryRow[] = [];

  for (const testCase of validationCases) {
    expectedCases.push({
      testCase,
      oracle: await oracleResult(
        testCase.fixture,
        testCase.top,
        testCase.maxWord,
      ),
    });
  }

  for (const implementation of implementations) {
    for (const { testCase, oracle } of expectedCases) {
      const result = await runJson(
        implementation,
        testCase.fixture,
        testCase.top,
        testCase.maxWord,
      );
      assertSame(`${implementation.name} (${testCase.name})`, oracle, result);
    }
    rows.push({ name: implementation.name, status: "ok" });
  }

  if (!options.validateOnly) {
    const timings = await benchmarkAll(
      implementations,
      options.fixture,
      options.top,
      options.maxWord,
      options.runs,
      options.warmups,
    );
    for (const row of rows) {
      const timing = timings.get(row.name);
      if (timing !== undefined) {
        row.timing = timing;
      }
    }
  }

  printSummary(rows);
}

function parseArgs(args: string[]): BenchOptions {
  const parsed: BenchOptions = {
    fixture: process.env.WFC_FIXTURE ?? benchmarkFixture,
    top: parseDecimal(process.env.WFC_TOP ?? "10", "WFC_TOP"),
    maxWord: parseDecimal(process.env.WFC_MAX_WORD ?? "1024", "WFC_MAX_WORD"),
    runs: 5,
    warmups: parseDecimal(process.env.WFC_WARMUPS ?? "3", "WFC_WARMUPS"),
    buildOnly: false,
    validateOnly: false,
  };

  for (const arg of args) {
    if (arg === "--build-only") parsed.buildOnly = true;
    else if (arg === "--validate") parsed.validateOnly = true;
    else if (arg.startsWith("--fixture="))
      parsed.fixture = arg.slice("--fixture=".length);
    else if (arg.startsWith("--top="))
      parsed.top = parseDecimal(arg.slice("--top=".length), "--top");
    else if (arg.startsWith("--max-word="))
      parsed.maxWord = parseDecimal(
        arg.slice("--max-word=".length),
        "--max-word",
      );
    else if (arg.startsWith("--runs="))
      parsed.runs = parseDecimal(arg.slice("--runs=".length), "--runs");
    else if (arg.startsWith("--warmups="))
      parsed.warmups = parseDecimal(
        arg.slice("--warmups=".length),
        "--warmups",
      );
    else throw new Error(`unknown option: ${arg}`);
  }

  if (parsed.top <= 0) throw new Error("--top must be greater than 0");
  if (parsed.runs <= 0) throw new Error("--runs must be greater than 0");

  parsed.fixture = resolve(root, parsed.fixture);
  return parsed;
}

function createValidationCases(options: BenchOptions): ValidationCase[] {
  const requested = {
    name: "requested",
    fixture: options.fixture,
    top: options.top,
    maxWord: options.maxWord,
  };

  const generated = [
    {
      name: "empty",
      content: "",
      top: 10,
      maxWord: 1024,
    },
    {
      name: "separators",
      content: new Uint8Array([0, 9, 10, 32, 33, 45, 48, 57, 95, 255]),
      top: 10,
      maxWord: 1024,
    },
    {
      name: "ascii-contract",
      content: "Apple apple APPLE\nfoo-bar\n123abc456\nit's\nz b a\n",
      top: 20,
      maxWord: 1024,
    },
    {
      name: "non-ascii-bytes",
      content: new Uint8Array([
        99, 97, 102, 195, 169, 32, 110, 97, 195, 175, 118, 101, 32, 0, 65, 90,
      ]),
      top: 10,
      maxWord: 1024,
    },
    {
      name: "default-max-word",
      content: `${"A".repeat(80)} ${"a".repeat(64)} ${"b".repeat(65)}\n`,
      top: 10,
      maxWord: 0,
    },
    {
      name: "min-word-clamp",
      content: "alphabet alpha alphanumeric ALPHABET\n",
      top: 10,
      maxWord: 1,
    },
    {
      name: "top-limit",
      content: "b a c a b a d d d e\n",
      top: 3,
      maxWord: 1024,
    },
  ];

  return [
    requested,
    ...generated.map((testCase) => ({
      name: testCase.name,
      fixture: writeValidationFixture(testCase.name, testCase.content),
      top: testCase.top,
      maxWord: testCase.maxWord,
    })),
  ];
}

function writeValidationFixture(name: string, content: string | Uint8Array) {
  const fixture = join(validationFixtures, `${name}.txt`);
  writeFileSync(fixture, content);
  return fixture;
}

function createBenchmarkFixture() {
  const parts: string[] = [];

  for (let index = 0; index < 32_000; index += 1) {
    parts.push(
      benchmarkWords[index % benchmarkWords.length],
      benchmarkSeparators[index % benchmarkSeparators.length],
    );
  }
  for (let index = 0; index < 8_000; index += 1) {
    parts.push(
      syntheticWord(index),
      benchmarkSeparators[(index + 3) % benchmarkSeparators.length],
    );
  }

  return `${parts.join("")}\n`;
}

function syntheticWord(value: number) {
  const suffix = value % 5;
  let number = value;
  let word = "";
  for (let index = 0; index < 8; index += 1) {
    word += String.fromCharCode(97 + (number % 26));
    number = Math.floor(number / 26);
  }
  return word + "x".repeat(suffix);
}

function parseDecimal(value: string, name: string) {
  if (!/^[0-9]+$/.test(value)) {
    throw new Error(`${name} must be a whole decimal number`);
  }
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed)) {
    throw new Error(`${name} is too large`);
  }
  return parsed;
}

async function ensureOracle() {
  const oracle = process.env.WFC_ORACLE ?? defaultOracle;
  try {
    statSync(oracle);
  } catch {
    await run({
      cwd: tokenfreqRoot,
      cmd: "cmake",
      args: ["--preset", "clang"],
    });
    await run({
      cwd: tokenfreqRoot,
      cmd: "cmake",
      args: ["--build", "--preset", "clang"],
    });
  }
}

async function buildAll() {
  for (const implementation of implementations) {
    for (const command of implementation.build ?? []) {
      await run(command);
    }
  }
}

async function oracleResult(
  fixture: string,
  top: number,
  maxWord: number,
): Promise<JsonResult> {
  const oracle = process.env.WFC_ORACLE ?? defaultOracle;
  const output = await run({
    cmd: oracle,
    args: [
      "--format",
      "json",
      "-n",
      String(top),
      "--max-word",
      String(maxWord),
      fixture,
    ],
  });
  const parsed = JSON.parse(output) as {
    words: { word: string; count: number }[];
    summary: { total: number; unique: number };
  };
  return {
    total: parsed.summary.total,
    unique: parsed.summary.unique,
    top: parsed.words.map((entry) => ({
      word: entry.word,
      count: entry.count,
    })),
  };
}

async function runJson(
  implementation: Implementation,
  fixture: string,
  top: number,
  maxWord: number,
): Promise<JsonResult> {
  const output = await run(implementation.run(fixture, top, maxWord));
  return JSON.parse(output) as JsonResult;
}

async function benchmarkAll(
  implementations: Implementation[],
  fixture: string,
  top: number,
  maxWord: number,
  runs: number,
  warmups: number,
): Promise<Map<string, BenchmarkResult>> {
  const states: BenchmarkState[] = implementations.map((implementation) => ({
    implementation,
    command: implementation.run(fixture, top, maxWord),
    startupCommand: implementation.run(startupFixture, top, maxWord),
    totalSamples: [],
    startupSamples: [],
    adjustedSamples: [],
  }));

  for (let index = 0; index < warmups; index += 1) {
    for (const state of rotated(states, index)) {
      await run(state.command);
      await run(state.startupCommand);
    }
  }

  for (let index = 0; index < runs; index += 1) {
    for (const state of rotated(states, index)) {
      const totalMs = await timeRun(state.command);
      const startupMs = await timeRun(state.startupCommand);
      state.totalSamples.push(totalMs);
      state.startupSamples.push(startupMs);
      state.adjustedSamples.push(Math.max(0, totalMs - startupMs));
    }
  }

  return new Map(
    states.map((state) => [
      state.implementation.name,
      {
        totalMeanMs: mean(state.totalSamples),
        startupMeanMs: mean(state.startupSamples),
        adjustedMeanMs: mean(state.adjustedSamples),
        adjustedP95Ms: percentile(state.adjustedSamples, 0.95),
      },
    ]),
  );
}

function rotated<T>(items: T[], offset: number) {
  const start = offset % items.length;
  return [...items.slice(start), ...items.slice(0, start)];
}

function mean(samples: number[]) {
  return samples.reduce((sum, sample) => sum + sample, 0) / samples.length;
}

function percentile(samples: number[], quantile: number) {
  const sorted = [...samples].sort((left, right) => left - right);
  return sorted[
    Math.min(sorted.length - 1, Math.ceil(sorted.length * quantile) - 1)
  ];
}

async function timeRun(command: Command): Promise<number> {
  const started = performance.now();
  await run(command);
  return performance.now() - started;
}

function assertSame(name: string, expected: JsonResult, actual: JsonResult) {
  const expectedText = JSON.stringify(expected);
  const actualText = JSON.stringify(actual);
  if (expectedText !== actualText) {
    throw new Error(
      `${name} did not match tokenfreq-c99\nexpected ${expectedText}\nactual   ${actualText}`,
    );
  }
}

function printSummary(rows: SummaryRow[]) {
  const hasTimings = rows.some((row) => row.timing !== undefined);
  if (!hasTimings) {
    console.log("| implementation | status |");
    console.log("|---|---:|");
    for (const row of rows) {
      console.log(`| ${row.name} | ${row.status} |`);
    }
    return;
  }

  const displayRows = [...rows].sort(
    (left, right) =>
      (left.timing?.adjustedMeanMs ?? Number.POSITIVE_INFINITY) -
      (right.timing?.adjustedMeanMs ?? Number.POSITIVE_INFINITY),
  );

  console.log(
    "| implementation | status | raw mean ms | startup mean ms | adjusted mean ms | adjusted p95 ms |",
  );
  console.log("|---|---:|---:|---:|---:|---:|");
  for (const row of displayRows) {
    const timing = row.timing;
    console.log(
      `| ${row.name} | ${row.status} | ${timing === undefined ? "" : timing.totalMeanMs.toFixed(3)} | ${timing === undefined ? "" : timing.startupMeanMs.toFixed(3)} | ${timing === undefined ? "" : timing.adjustedMeanMs.toFixed(3)} | ${timing === undefined ? "" : timing.adjustedP95Ms.toFixed(3)} |`,
    );
  }
}

async function run(command: Command): Promise<string> {
  return await new Promise((resolveCommand, reject) => {
    const child = spawn(command.cmd, command.args, {
      cwd: command.cwd ?? root,
      stdio: ["ignore", "pipe", "pipe"],
      env: process.env,
    });
    const stdout: Buffer[] = [];
    const stderr: Buffer[] = [];
    child.stdout.on("data", (chunk) => stdout.push(chunk));
    child.stderr.on("data", (chunk) => stderr.push(chunk));
    child.on("error", reject);
    child.on("close", (code) => {
      if (code === 0) {
        resolveCommand(Buffer.concat(stdout).toString("utf8"));
      } else {
        const out = Buffer.concat(stdout).toString("utf8").trim();
        const err = Buffer.concat(stderr).toString("utf8").trim();
        const detail = [out, err].filter(Boolean).join("\n");
        reject(
          new Error(
            `${command.cmd} ${command.args.join(" ")} failed with ${code}\n${detail}`,
          ),
        );
      }
    });
  });
}
