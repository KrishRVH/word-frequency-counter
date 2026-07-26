import { FileSystem } from '@effect/platform';
import { NodeContext, NodeRuntime } from '@effect/platform-node';
import { Console, Data, Effect, Schema } from 'effect';

type Entry = Readonly<{ word: string; count: number }>;
type Result = Readonly<{ total: number; unique: number; top: readonly Entry[] }>;
type Options = Readonly<{
  path: string;
  top: number;
  maxWord: number;
  benchRuns: number;
  benchWarmups: number;
  json: boolean;
}>;

const checksumOffset = 2_166_136_261;
const checksumPrime = 16_777_619;
const usage = 'usage: wordcount.ts [--json] [--top N] [--max-word N] <file>';
const resultJson = Schema.parseJson(
  Schema.Struct({
    total: Schema.Number,
    unique: Schema.Number,
    top: Schema.Array(Schema.Struct({ word: Schema.String, count: Schema.Number })),
  }),
);
const benchJson = Schema.parseJson(Schema.Struct({ mean_ms: Schema.Number, checksum: Schema.Number }));

class UsageError extends Data.TaggedError('UsageError')<{ message: string }> {}

function normalizeMaxWord(value: number): number {
  return value === 0 ? 64 : Math.min(1024, Math.max(4, value));
}

function countWords(bytes: Uint8Array, limit: number, requestedMaxWord: number): Result {
  const counts = new Map<string, number>();
  const maxWord = normalizeMaxWord(requestedMaxWord);
  let word = '';
  let total = 0;

  for (const byte of bytes) {
    const lower = byte | 0x20;
    if (lower >= 97 && lower <= 122) {
      if (word.length < maxWord) {
        word += String.fromCharCode(lower);
      }
    } else if (word.length > 0) {
      counts.set(word, (counts.get(word) ?? 0) + 1);
      total += 1;
      word = '';
    }
  }
  if (word.length > 0) {
    counts.set(word, (counts.get(word) ?? 0) + 1);
    total += 1;
  }

  const top = [...counts]
    .map(([entryWord, count]) => ({ word: entryWord, count }))
    .sort((left, right) => {
      const countOrder = right.count - left.count;
      return countOrder !== 0 ? countOrder : left.word < right.word ? -1 : left.word > right.word ? 1 : 0;
    })
    .slice(0, limit);
  return { total, unique: counts.size, top };
}

function mixByte(checksumValue: number, byte: number): number {
  return Math.imul(checksumValue ^ byte, checksumPrime) >>> 0;
}

function mixInteger(checksumValue: number, value: number, width: number): number {
  let mixed = checksumValue;
  let remaining = BigInt(value);
  for (let index = 0; index < width; index += 1) {
    mixed = mixByte(mixed, Number(remaining & 0xffn));
    remaining >>= 8n;
  }
  return mixed;
}

function checksum(result: Result): number {
  let value = mixInteger(checksumOffset, result.total, 8);
  value = mixInteger(value, result.unique, 8);
  for (const entry of result.top) {
    for (let index = 0; index < entry.word.length; index += 1) {
      value = mixByte(value, entry.word.charCodeAt(index));
    }
    value = mixInteger(value, entry.count, 8);
  }
  return value;
}

function renderBench(bytes: Uint8Array, options: Options): string {
  for (let index = 0; index < options.benchWarmups; index += 1) {
    checksum(countWords(bytes, options.top, options.maxWord));
  }

  let value = checksumOffset;
  const started = Bun.nanoseconds();
  for (let index = 0; index < options.benchRuns; index += 1) {
    value = mixInteger(value, checksum(countWords(bytes, options.top, options.maxWord)), 4);
  }
  const meanMs = (Bun.nanoseconds() - started) / 1_000_000 / options.benchRuns;
  return Schema.encodeSync(benchJson)({ mean_ms: meanMs, checksum: value });
}

function renderText(result: Result): string {
  const lines = ['count word'];
  for (const entry of result.top) {
    lines.push(`${entry.count} ${entry.word}`);
  }
  lines.push(`total ${result.total}`, `unique ${result.unique}`);
  return lines.join('\n');
}

function parseNumber(value: string | undefined, name: string): number {
  if (value === undefined || !/^\d+$/.test(value)) {
    throw new Error(`${name} must be a number`);
  }
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed)) {
    throw new Error(`${name} must be a number`);
  }
  return parsed;
}

function parseArgs(args: readonly string[]): Options {
  let path: string | undefined;
  let top = 10;
  let maxWord = 1024;
  let benchRuns = 0;
  let benchWarmups = 0;
  let json = false;

  for (let index = 0; index < args.length; index += 1) {
    const arg = args[index];
    if (arg === undefined) {
      throw new Error(usage);
    }
    if (arg === '--json') {
      json = true;
    } else if (arg === '--top' || arg === '--max-word' || arg === '--bench-runs' || arg === '--bench-warmups') {
      const value = parseNumber(args[++index], arg);
      if (arg === '--top') {
        top = value;
      } else if (arg === '--max-word') {
        maxWord = value;
      } else if (arg === '--bench-runs') {
        benchRuns = value;
      } else {
        benchWarmups = value;
      }
    } else if (arg.startsWith('--top=')) {
      top = parseNumber(arg.slice(6), '--top');
    } else if (arg.startsWith('--max-word=')) {
      maxWord = parseNumber(arg.slice(11), '--max-word');
    } else if (arg.startsWith('--bench-runs=')) {
      benchRuns = parseNumber(arg.slice(13), '--bench-runs');
    } else if (arg.startsWith('--bench-warmups=')) {
      benchWarmups = parseNumber(arg.slice(16), '--bench-warmups');
    } else if (arg.startsWith('-') || path !== undefined) {
      throw new Error(usage);
    } else {
      path = arg;
    }
  }

  if (path === undefined || top === 0) {
    throw new Error(usage);
  }
  return { path, top, maxWord, benchRuns, benchWarmups, json };
}

const program = Effect.gen(function* () {
  const options = yield* Effect.try({
    try: () => parseArgs(process.argv.slice(2)),
    catch: (cause) =>
      new UsageError({
        message: cause instanceof Error ? cause.message : String(cause),
      }),
  });
  const fileSystem = yield* FileSystem.FileSystem;
  const bytes = yield* fileSystem.readFile(options.path);
  if (options.benchRuns > 0) {
    yield* Console.log(renderBench(bytes, options));
  } else {
    const result = countWords(bytes, options.top, options.maxWord);
    const output = options.json ? yield* Schema.encode(resultJson)(result) : renderText(result);
    yield* Console.log(output);
  }
});

NodeRuntime.runMain(
  program.pipe(
    Effect.tapError((error) => Console.error(error.message)),
    Effect.provide(NodeContext.layer),
  ),
  { disableErrorReporting: true },
);
