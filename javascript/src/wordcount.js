#!/usr/bin/env node
// @ts-check

import { readFileSync } from "node:fs";
import { performance } from "node:perf_hooks";
import { pathToFileURL } from "node:url";

/**
 * @typedef {{ word: string, count: number }} Entry
 * @typedef {{ total: number, unique: number, top: Entry[] }} Result
 * @typedef {{ path: string, top: number, maxWord: number, benchRuns: number, benchWarmups: number, json: boolean }} Options
 */

const oracleDefaultMaxWord = 64;
const maxWordLimit = 1024;
const minWord = 4;
const checksumOffset = 2_166_136_261;
const checksumPrime = 16_777_619;
const usage = "usage: wordcount_js [--json] [--top N] [--max-word N] <file>";

/**
 * @param {Uint8Array} bytes
 * @param {number} limit
 * @param {number} maxWord
 * @returns {Result}
 */
export function countWords(bytes, limit = 10, maxWord = maxWordLimit) {
  /** @type {Map<string, number>} */
  const counts = new Map();
  let word = "";
  let total = 0;
  const normalizedMaxWord = normalizeMaxWord(maxWord);

  for (const byte of bytes) {
    if (isLetter(byte)) {
      if (word.length < normalizedMaxWord) {
        word += String.fromCharCode(lowerAscii(byte));
      }
      continue;
    }

    if (word.length > 0) {
      counts.set(word, (counts.get(word) ?? 0) + 1);
      total += 1;
      word = "";
    }
  }

  if (word.length > 0) {
    counts.set(word, (counts.get(word) ?? 0) + 1);
    total += 1;
  }

  const top = [...counts.entries()]
    .map(([entryWord, count]) => ({ word: entryWord, count }))
    .sort(
      (left, right) =>
        right.count - left.count || compareAscii(left.word, right.word),
    )
    .slice(0, limit);

  return { total, unique: counts.size, top };
}

/**
 * @param {Result} result
 * @returns {string}
 */
export function renderText(result) {
  const rows = ["count word"];

  for (const entry of result.top) {
    rows.push(`${entry.count} ${entry.word}`);
  }
  rows.push(`total ${result.total}`);
  rows.push(`unique ${result.unique}`);
  return `${rows.join("\n")}\n`;
}

/**
 * @param {number} byte
 * @returns {boolean}
 */
function isLetter(byte) {
  const lower = byte | 32;
  return lower >= 97 && lower <= 122;
}

/**
 * @param {number} byte
 * @returns {number}
 */
function lowerAscii(byte) {
  return byte >= 65 && byte <= 90 ? byte + 32 : byte;
}

/**
 * @param {string} left
 * @param {string} right
 * @returns {number}
 */
function compareAscii(left, right) {
  return left < right ? -1 : left > right ? 1 : 0;
}

/**
 * @param {string[]} args
 * @returns {Options}
 */
function parseArgs(args) {
  let path;
  let top = 10;
  let maxWord = maxWordLimit;
  let benchRuns = 0;
  let benchWarmups = 0;
  let json = false;

  for (let index = 0; index < args.length; index += 1) {
    const arg = args[index];
    if (arg === undefined) {
      break;
    }

    if (arg === "--json") {
      json = true;
    } else if (arg === "--top") {
      index += 1;
      top = parseNumber(args[index]);
    } else if (arg.startsWith("--top=")) {
      top = parseNumber(arg.slice("--top=".length));
    } else if (arg === "--max-word") {
      index += 1;
      maxWord = parseNumber(args[index]);
    } else if (arg.startsWith("--max-word=")) {
      maxWord = parseNumber(arg.slice("--max-word=".length));
    } else if (arg === "--bench-runs") {
      index += 1;
      benchRuns = parseNumber(args[index]);
    } else if (arg.startsWith("--bench-runs=")) {
      benchRuns = parseNumber(arg.slice("--bench-runs=".length));
    } else if (arg === "--bench-warmups") {
      index += 1;
      benchWarmups = parseNumber(args[index]);
    } else if (arg.startsWith("--bench-warmups=")) {
      benchWarmups = parseNumber(arg.slice("--bench-warmups=".length));
    } else if (arg.startsWith("-")) {
      throw new Error(usage);
    } else if (path === undefined) {
      path = arg;
    } else {
      throw new Error(usage);
    }
  }

  if (
    path === undefined ||
    !Number.isInteger(top) ||
    top <= 0 ||
    !Number.isInteger(maxWord) ||
    !Number.isInteger(benchRuns) ||
    !Number.isInteger(benchWarmups)
  ) {
    throw new Error(usage);
  }

  return {
    path,
    top,
    maxWord: normalizeMaxWord(maxWord),
    benchRuns,
    benchWarmups,
    json,
  };
}

/**
 * @param {string | undefined} value
 * @returns {number}
 */
function parseNumber(value) {
  if (value === undefined || !/^[0-9]+$/.test(value)) {
    return Number.NaN;
  }
  const parsed = Number(value);
  return Number.isSafeInteger(parsed) ? parsed : Number.NaN;
}

/**
 * @param {number} value
 * @returns {number}
 */
function normalizeMaxWord(value) {
  if (value === 0) {
    return oracleDefaultMaxWord;
  }
  return Math.min(Math.max(value, minWord), maxWordLimit);
}

/**
 * @param {Uint8Array} bytes
 * @param {Options} options
 * @returns {string}
 */
function renderBench(bytes, options) {
  for (let index = 0; index < options.benchWarmups; index += 1) {
    checksum(countWords(bytes, options.top, options.maxWord));
  }

  let checksumValue = checksumOffset;
  const started = performance.now();
  for (let index = 0; index < options.benchRuns; index += 1) {
    checksumValue = mixUint32(
      checksumValue,
      checksum(countWords(bytes, options.top, options.maxWord)),
    );
  }
  const meanMs = (performance.now() - started) / options.benchRuns;

  return `${JSON.stringify({ mean_ms: meanMs, checksum: checksumValue })}\n`;
}

/**
 * @param {Result} result
 * @returns {number}
 */
function checksum(result) {
  let value = checksumOffset;
  value = mixUint64(value, result.total);
  value = mixUint64(value, result.unique);
  for (const entry of result.top) {
    for (let index = 0; index < entry.word.length; index += 1) {
      value = mixByte(value, entry.word.charCodeAt(index));
    }
    value = mixUint64(value, entry.count);
  }
  return value;
}

/**
 * @param {number} checksumValue
 * @param {number} value
 * @returns {number}
 */
function mixByte(checksumValue, value) {
  return Math.imul(checksumValue ^ value, checksumPrime) >>> 0;
}

/**
 * @param {number} checksumValue
 * @param {number} value
 * @returns {number}
 */
function mixUint32(checksumValue, value) {
  let remaining = BigInt(value);
  let mixed = checksumValue;
  for (let index = 0; index < 4; index += 1) {
    mixed = mixByte(mixed, Number(remaining & 0xffn));
    remaining >>= 8n;
  }
  return mixed;
}

/**
 * @param {number} checksumValue
 * @param {number} value
 * @returns {number}
 */
function mixUint64(checksumValue, value) {
  let remaining = BigInt(value);
  let mixed = checksumValue;
  for (let index = 0; index < 8; index += 1) {
    mixed = mixByte(mixed, Number(remaining & 0xffn));
    remaining >>= 8n;
  }
  return mixed;
}

const invokedUrl = process.argv[1] && pathToFileURL(process.argv[1]).href;
if (import.meta.url === invokedUrl) {
  try {
    const options = parseArgs(process.argv.slice(2));
    const bytes = readFileSync(options.path);
    if (options.benchRuns > 0) {
      process.stdout.write(renderBench(bytes, options));
      process.exitCode = 0;
    } else {
      const result = countWords(bytes, options.top, options.maxWord);
      process.stdout.write(
        options.json ? `${JSON.stringify(result)}\n` : renderText(result),
      );
    }
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    process.stderr.write(`wordcount_js: ${message}\n`);
    process.exitCode = 1;
  }
}
