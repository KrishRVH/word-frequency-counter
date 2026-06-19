import { mkdirSync, statSync } from "node:fs";
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

const root = resolve(import.meta.dir, "..");
const buildBin = join(root, "build", "bin");
const haskellBuild = join(root, "build", "haskell");
const tokenfreqRoot = join(homedir(), "dev/personal/tokenfreq-c99");
const defaultOracle = join(tokenfreqRoot, "build/clang/wc");

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
      cmd: "dotnet",
      args: [
        "run",
        "--project",
        "csharp/src/WordFrequencyCounter/WordFrequencyCounter.csproj",
        "-c",
        "Release",
        "--no-build",
        "--",
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
mkdirSync(haskellBuild, { recursive: true });

await ensureOracle();
await buildAll();

if (!options.buildOnly) {
  const oracle = await oracleResult(
    options.fixture,
    options.top,
    options.maxWord,
  );
  const rows: { name: string; status: string; meanMs?: number }[] = [];

  for (const implementation of implementations) {
    const result = await runJson(
      implementation,
      options.fixture,
      options.top,
      options.maxWord,
    );
    assertSame(implementation.name, oracle, result);
    const meanMs = options.validateOnly
      ? undefined
      : await benchmark(
          implementation,
          options.fixture,
          options.top,
          options.maxWord,
          options.runs,
        );
    rows.push({ name: implementation.name, status: "ok", meanMs });
  }

  printSummary(rows);
}

function parseArgs(args: string[]) {
  const parsed = {
    fixture: process.env.WFC_FIXTURE ?? "fixtures/spec.txt",
    top: parseDecimal(process.env.WFC_TOP ?? "10", "WFC_TOP"),
    maxWord: parseDecimal(process.env.WFC_MAX_WORD ?? "1024", "WFC_MAX_WORD"),
    runs: 5,
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
    else throw new Error(`unknown option: ${arg}`);
  }

  if (parsed.top <= 0) throw new Error("--top must be greater than 0");
  if (parsed.runs <= 0) throw new Error("--runs must be greater than 0");

  parsed.fixture = resolve(root, parsed.fixture);
  return parsed;
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

async function benchmark(
  implementation: Implementation,
  fixture: string,
  top: number,
  maxWord: number,
  runs: number,
): Promise<number> {
  const samples: number[] = [];
  const command = implementation.run(fixture, top, maxWord);
  for (let index = 0; index < runs; index += 1) {
    const started = performance.now();
    await run(command);
    samples.push(performance.now() - started);
  }
  return samples.reduce((sum, sample) => sum + sample, 0) / samples.length;
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
  rows: { name: string; status: string; meanMs?: number }[],
) {
  console.log("| implementation | status | mean ms |");
  console.log("|---|---:|---:|");
  for (const row of rows) {
    console.log(
      `| ${row.name} | ${row.status} | ${row.meanMs === undefined ? "" : row.meanMs.toFixed(3)} |`,
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
