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
  corpus: string;
  fixture?: string;
  top: number;
  maxWord: number;
  runs: number;
  warmups: number;
  warmTaskSamples: number;
  warmTaskRuns: number;
  warmTaskWarmups: number;
  buildOnly: boolean;
  validateOnly: boolean;
};

type ValidationCase = {
  name: string;
  fixture: string;
  top: number;
  maxWord: number;
};

type CorpusProfile = "mixed" | "unique" | "repeated" | "long";

type CorpusSpec = {
  name: string;
  targetBytes: number;
  profile: CorpusProfile;
  description: string;
  primary?: boolean;
};

type BenchmarkFixture = {
  name: string;
  fixture: string;
  bytes: number;
  description: string;
  primary: boolean;
};

type BenchmarkState = {
  implementation: Implementation;
  command: Command;
  startupCommand: Command;
  warmTaskCommand: Command;
  warmTaskSamples: number[];
  totalSamples: number[];
  startupSamples: number[];
  adjustedSamples: number[];
};

type BenchmarkResult = {
  warmTaskMeanMs: number;
  totalMeanMs: number;
  startupMeanMs: number;
  adjustedMeanMs: number;
  adjustedP95Ms: number;
};

type WarmTaskResult = { mean_ms: number; checksum: number | string };

type SummaryRow = {
  name: string;
  timings: Map<string, BenchmarkResult>;
};

const root = resolve(import.meta.dir, "..");
const buildBin = join(root, "build", "bin");
const csharpBin = join(
  root,
  "csharp/src/WordFrequencyCounter/bin/Release/net10.0/WordFrequencyCounter" +
    (process.platform === "win32" ? ".exe" : ""),
);
const validationFixtures = join(root, "build", "fixtures");
const legacyBenchmarkFixture = join(validationFixtures, "benchmark.txt");
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
const corpusDefinitions = new Map<string, CorpusSpec[]>([
  [
    "default",
    [
      {
        name: "tiny-mix",
        targetBytes: 4 * 1024,
        profile: "mixed",
        description: "startup-sensitive smoke fixture",
      },
      {
        name: "small-mix",
        targetBytes: 64 * 1024,
        profile: "mixed",
        description: "fits comfortably in cache",
      },
      {
        name: "medium-mix",
        targetBytes: 512 * 1024,
        profile: "mixed",
        description: "primary mixed repeated/unique workload",
        primary: true,
      },
      {
        name: "unique-sort",
        targetBytes: 512 * 1024,
        profile: "unique",
        description: "mostly unique words, stressing allocation and sort",
      },
    ],
  ],
  [
    "stress",
    [
      {
        name: "tiny-mix",
        targetBytes: 4 * 1024,
        profile: "mixed",
        description: "startup-sensitive smoke fixture",
      },
      {
        name: "small-mix",
        targetBytes: 64 * 1024,
        profile: "mixed",
        description: "fits comfortably in cache",
      },
      {
        name: "medium-mix",
        targetBytes: 512 * 1024,
        profile: "mixed",
        description: "primary mixed repeated/unique workload",
        primary: true,
      },
      {
        name: "unique-sort",
        targetBytes: 512 * 1024,
        profile: "unique",
        description: "mostly unique words, stressing allocation and sort",
      },
      {
        name: "repeated-scan",
        targetBytes: 1 * 1024 * 1024,
        profile: "repeated",
        description: "large low-cardinality scanner and hot-map case",
      },
      {
        name: "long-clamp",
        targetBytes: 256 * 1024,
        profile: "long",
        description: "long words that exercise max-word clamping",
      },
    ],
  ],
]);

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
const benchmarkFixtures = createBenchmarkFixtures(options);
writeFileSync(startupFixture, "");

await ensureOracle();
await buildAll();

if (!options.buildOnly) {
  const validationCases = createValidationCases(options, benchmarkFixtures);
  const expectedCases: { testCase: ValidationCase; oracle: JsonResult }[] = [];
  const rows: SummaryRow[] = [];
  const expectedByName = new Map<string, JsonResult>();

  for (const testCase of validationCases) {
    const expectedCase = {
      testCase,
      oracle: await oracleResult(
        testCase.fixture,
        testCase.top,
        testCase.maxWord,
      ),
    };
    expectedCases.push(expectedCase);
    expectedByName.set(testCase.name, expectedCase.oracle);
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
    rows.push({ name: implementation.name, timings: new Map() });
  }

  if (!options.validateOnly) {
    for (const benchmarkFixture of benchmarkFixtures) {
      const expected = expectedByName.get(
        validationNameForBenchmarkFixture(benchmarkFixture),
      );
      if (expected === undefined) {
        throw new Error(`missing oracle result for ${benchmarkFixture.name}`);
      }
      const timings = await benchmarkAll(
        implementations,
        benchmarkFixture.fixture,
        options.top,
        options.maxWord,
        options.runs,
        options.warmups,
        options.warmTaskSamples,
        options.warmTaskRuns,
        options.warmTaskWarmups,
        checksumResult(expected),
      );
      for (const row of rows) {
        const timing = timings.get(row.name);
        if (timing !== undefined) {
          row.timings.set(benchmarkFixture.name, timing);
        }
      }
    }
  }

  printSummary(rows, benchmarkFixtures);
}

function parseArgs(args: string[]): BenchOptions {
  const envCorpus = process.env.WFC_CORPUS;
  const envFixture =
    envCorpus === undefined && process.env.WFC_FIXTURE !== ""
      ? process.env.WFC_FIXTURE
      : undefined;
  const parsed: BenchOptions = {
    corpus: envCorpus ?? "default",
    fixture: envFixture,
    top: parseDecimal(process.env.WFC_TOP ?? "10", "WFC_TOP"),
    maxWord: parseDecimal(process.env.WFC_MAX_WORD ?? "1024", "WFC_MAX_WORD"),
    runs: 5,
    warmups: parseDecimal(process.env.WFC_WARMUPS ?? "3", "WFC_WARMUPS"),
    warmTaskSamples: parseDecimal(
      process.env.WFC_WARM_TASK_SAMPLES ?? "3",
      "WFC_WARM_TASK_SAMPLES",
    ),
    warmTaskRuns: parseDecimal(
      process.env.WFC_WARM_TASK_RUNS ?? "50",
      "WFC_WARM_TASK_RUNS",
    ),
    warmTaskWarmups: parseDecimal(
      process.env.WFC_WARM_TASK_WARMUPS ?? "10",
      "WFC_WARM_TASK_WARMUPS",
    ),
    buildOnly: false,
    validateOnly: false,
  };

  for (const arg of args) {
    if (arg === "--build-only") parsed.buildOnly = true;
    else if (arg === "--validate") parsed.validateOnly = true;
    else if (arg.startsWith("--fixture="))
      parsed.fixture = arg.slice("--fixture=".length);
    else if (arg.startsWith("--corpus=")) {
      parsed.corpus = arg.slice("--corpus=".length);
      parsed.fixture = undefined;
    } else if (arg.startsWith("--top="))
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
    else if (arg.startsWith("--warm-task-samples="))
      parsed.warmTaskSamples = parseDecimal(
        arg.slice("--warm-task-samples=".length),
        "--warm-task-samples",
      );
    else if (arg.startsWith("--warm-task-runs="))
      parsed.warmTaskRuns = parseDecimal(
        arg.slice("--warm-task-runs=".length),
        "--warm-task-runs",
      );
    else if (arg.startsWith("--warm-task-warmups="))
      parsed.warmTaskWarmups = parseDecimal(
        arg.slice("--warm-task-warmups=".length),
        "--warm-task-warmups",
      );
    else throw new Error(`unknown option: ${arg}`);
  }

  if (parsed.top <= 0) throw new Error("--top must be greater than 0");
  if (parsed.runs <= 0) throw new Error("--runs must be greater than 0");
  if (parsed.warmTaskSamples <= 0)
    throw new Error("--warm-task-samples must be greater than 0");
  if (parsed.warmTaskRuns <= 0)
    throw new Error("--warm-task-runs must be greater than 0");

  if (parsed.fixture !== undefined) {
    parsed.fixture = resolve(root, parsed.fixture);
  }
  return parsed;
}

function createValidationCases(
  options: BenchOptions,
  benchmarkFixtures: BenchmarkFixture[],
): ValidationCase[] {
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
    ...benchmarkFixtures.map((benchmarkFixture) => ({
      name: validationNameForBenchmarkFixture(benchmarkFixture),
      fixture: benchmarkFixture.fixture,
      top: options.top,
      maxWord: options.maxWord,
    })),
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

function createBenchmarkFixtures(options: BenchOptions): BenchmarkFixture[] {
  if (options.fixture !== undefined) {
    return [
      {
        name: "custom",
        fixture: options.fixture,
        bytes: statSync(options.fixture).size,
        description: "custom fixture",
        primary: true,
      },
    ];
  }

  const specs = corpusDefinitions.get(options.corpus);
  if (specs === undefined) {
    throw new Error(
      `unknown corpus: ${options.corpus}; expected ${[
        ...corpusDefinitions.keys(),
      ].join(", ")}`,
    );
  }

  const hasPrimary = specs.some((spec) => spec.primary === true);
  return specs.map((spec, index) => {
    const content = createCorpusFixture(spec);
    const fixture = writeBenchmarkFixture(spec.name, content);
    if (spec.primary === true || (!hasPrimary && index === 0)) {
      writeFileSync(legacyBenchmarkFixture, content);
    }
    return {
      name: spec.name,
      fixture,
      bytes: content.length,
      description: spec.description,
      primary: spec.primary === true || (!hasPrimary && index === 0),
    };
  });
}

function createCorpusFixture(spec: CorpusSpec) {
  let content = "";
  let index = 0;

  while (content.length < spec.targetBytes) {
    content += corpusWord(spec.profile, index);
    content += benchmarkSeparators[index % benchmarkSeparators.length];
    index += 1;
  }

  return content.endsWith("\n") ? content : `${content}\n`;
}

function corpusWord(profile: CorpusProfile, index: number) {
  if (profile === "unique") {
    return syntheticWord(index) + syntheticWord(index + 17);
  }
  if (profile === "repeated") {
    return benchmarkWords[index % benchmarkWords.length];
  }
  if (profile === "long") {
    const base = benchmarkWords[index % benchmarkWords.length].toLowerCase();
    return `${base}${syntheticWord(index).repeat(16)}`;
  }
  if (index % 5 === 0) {
    return syntheticWord(index);
  }
  return benchmarkWords[index % benchmarkWords.length];
}

function writeBenchmarkFixture(name: string, content: string) {
  const fixture = join(validationFixtures, `bench-${name}.txt`);
  writeFileSync(fixture, content);
  return fixture;
}

function validationNameForBenchmarkFixture(benchmarkFixture: BenchmarkFixture) {
  return `bench:${benchmarkFixture.name}`;
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
  warmTaskSamples: number,
  warmTaskRuns: number,
  warmTaskWarmups: number,
  expectedWarmTaskChecksum: bigint,
): Promise<Map<string, BenchmarkResult>> {
  const states: BenchmarkState[] = implementations.map((implementation) => ({
    implementation,
    command: implementation.run(fixture, top, maxWord),
    startupCommand: implementation.run(startupFixture, top, maxWord),
    warmTaskCommand: warmTaskCommand(
      implementation.run(fixture, top, maxWord),
      warmTaskRuns,
      warmTaskWarmups,
    ),
    warmTaskSamples: [],
    totalSamples: [],
    startupSamples: [],
    adjustedSamples: [],
  }));

  for (const state of states) {
    await validateWarmTask(
      state.implementation.name,
      warmTaskCommand(state.command, 1, 0),
      expectedWarmTaskChecksum,
    );
  }

  for (let index = 0; index < warmTaskSamples; index += 1) {
    for (const state of rotated(states, index)) {
      state.warmTaskSamples.push(await runWarmTask(state.warmTaskCommand));
    }
  }

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
        warmTaskMeanMs: mean(state.warmTaskSamples),
        totalMeanMs: mean(state.totalSamples),
        startupMeanMs: mean(state.startupSamples),
        adjustedMeanMs: mean(state.adjustedSamples),
        adjustedP95Ms: percentile(state.adjustedSamples, 0.95),
      },
    ]),
  );
}

function warmTaskCommand(command: Command, runs: number, warmups: number) {
  const fixture = command.args.at(-1);
  if (fixture === undefined) {
    throw new Error(`${command.cmd} has no fixture argument`);
  }

  return {
    ...command,
    args: [
      ...command.args.slice(0, -1),
      "--bench-runs",
      String(runs),
      "--bench-warmups",
      String(warmups),
      fixture,
    ],
  };
}

async function runWarmTask(command: Command): Promise<number> {
  const output = await run(command);
  const result = JSON.parse(output) as WarmTaskResult;
  if (!Number.isFinite(result.mean_ms) || result.mean_ms < 0) {
    throw new Error(
      `${command.cmd} returned invalid warm-task mean: ${output}`,
    );
  }
  return result.mean_ms;
}

async function validateWarmTask(
  name: string,
  command: Command,
  expectedChecksum: bigint,
) {
  const output = await run(command);
  const result = JSON.parse(output) as WarmTaskResult;
  const actualChecksum = parseChecksum(result.checksum);
  if (actualChecksum !== expectedChecksum) {
    throw new Error(
      `${name} warm-task checksum mismatch\nexpected ${expectedChecksum}\nactual   ${actualChecksum}`,
    );
  }
}

function checksumResult(result: JsonResult) {
  let checksum = BigInt(result.total) ^ BigInt(result.unique);
  for (const entry of result.top) {
    checksum ^= BigInt(entry.count) ^ BigInt(entry.word.length);
  }
  return checksum;
}

function parseChecksum(value: number | string) {
  if (typeof value === "number") {
    if (!Number.isSafeInteger(value) || value < 0) {
      throw new Error(`invalid warm-task checksum: ${value}`);
    }
    return BigInt(value);
  }
  if (!/^[0-9]+$/.test(value)) {
    throw new Error(`invalid warm-task checksum: ${value}`);
  }
  return BigInt(value);
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

function printSummary(
  rows: SummaryRow[],
  benchmarkFixtures: BenchmarkFixture[],
) {
  const hasTimings = rows.some((row) => row.timings.size > 0);
  if (!hasTimings) {
    console.log("| implementation |");
    console.log("|---|");
    for (const row of rows) {
      console.log(`| ${row.name} |`);
    }
    return;
  }

  if (benchmarkFixtures.length > 1) {
    printCorpus(benchmarkFixtures);
    printCorpusSummary(rows, benchmarkFixtures);
    return;
  }

  printSingleFixtureSummary(rows, benchmarkFixtures[0]);
}

function printCorpus(benchmarkFixtures: BenchmarkFixture[]) {
  console.log("| fixture | bytes | role | notes |");
  console.log("|---|---:|---|---|");
  for (const benchmarkFixture of benchmarkFixtures) {
    console.log(
      `| ${benchmarkFixture.name} | ${benchmarkFixture.bytes} | ${
        benchmarkFixture.primary ? "primary" : ""
      } | ${benchmarkFixture.description} |`,
    );
  }
  console.log("");
}

function printCorpusSummary(
  rows: SummaryRow[],
  benchmarkFixtures: BenchmarkFixture[],
) {
  const primaryFixture =
    benchmarkFixtures.find((benchmarkFixture) => benchmarkFixture.primary) ??
    benchmarkFixtures[0];
  const displayRows = [...rows].sort(
    (left, right) =>
      (left.timings.get(primaryFixture.name)?.warmTaskMeanMs ??
        Number.POSITIVE_INFINITY) -
      (right.timings.get(primaryFixture.name)?.warmTaskMeanMs ??
        Number.POSITIVE_INFINITY),
  );
  const timingColumns = benchmarkFixtures
    .map((benchmarkFixture) => ` ${benchmarkFixture.name} warm ms `)
    .join("|");
  const alignmentColumns = benchmarkFixtures.map(() => "---:").join("|");

  console.log(
    `| implementation |${timingColumns}| ${primaryFixture.name} MB/s | ${primaryFixture.name} adjusted CLI ms |`,
  );
  console.log(`|---|${alignmentColumns}|---:|---:|`);
  for (const row of displayRows) {
    const timingCells = benchmarkFixtures
      .map((benchmarkFixture) =>
        formatMaybe(row.timings.get(benchmarkFixture.name)?.warmTaskMeanMs),
      )
      .join(" | ");
    const primaryTiming = row.timings.get(primaryFixture.name);
    console.log(
      `| ${row.name} | ${timingCells} | ${
        primaryTiming === undefined
          ? ""
          : throughputMiBPerSecond(primaryFixture.bytes, primaryTiming)
      } | ${formatMaybe(primaryTiming?.adjustedMeanMs)} |`,
    );
  }
}

function printSingleFixtureSummary(
  rows: SummaryRow[],
  benchmarkFixture: BenchmarkFixture,
) {
  const displayRows = [...rows].sort(
    (left, right) =>
      (left.timings.get(benchmarkFixture.name)?.warmTaskMeanMs ??
        Number.POSITIVE_INFINITY) -
      (right.timings.get(benchmarkFixture.name)?.warmTaskMeanMs ??
        Number.POSITIVE_INFINITY),
  );

  console.log(
    "| implementation | warm task mean ms | warm task MB/s | raw CLI mean ms | startup mean ms | adjusted CLI mean ms | adjusted CLI p95 ms |",
  );
  console.log("|---|---:|---:|---:|---:|---:|---:|");
  for (const row of displayRows) {
    const timing = row.timings.get(benchmarkFixture.name);
    console.log(
      `| ${row.name} | ${formatMaybe(timing?.warmTaskMeanMs)} | ${
        timing === undefined
          ? ""
          : throughputMiBPerSecond(benchmarkFixture.bytes, timing)
      } | ${formatMaybe(timing?.totalMeanMs)} | ${formatMaybe(
        timing?.startupMeanMs,
      )} | ${formatMaybe(timing?.adjustedMeanMs)} | ${formatMaybe(
        timing?.adjustedP95Ms,
      )} |`,
    );
  }
}

function formatMaybe(value: number | undefined) {
  return value === undefined ? "" : value.toFixed(3);
}

function throughputMiBPerSecond(bytes: number, timing: BenchmarkResult) {
  if (timing.warmTaskMeanMs === 0) {
    return "inf";
  }
  const mib = bytes / 1024 / 1024;
  return (mib / (timing.warmTaskMeanMs / 1000)).toFixed(1);
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
