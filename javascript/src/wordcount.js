#!/usr/bin/env node
// @ts-check

import { readFileSync } from "node:fs";
import { performance } from "node:perf_hooks";
import { parseArgs as parseNodeArgs } from "node:util";

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
function countWords(bytes, limit = 10, maxWord = maxWordLimit) {
  /** @type {Map<string, number>} */
  const counts = new Map();
  let word = "";
  let total = 0;
  const normalizedMaxWord = normalizeMaxWord(maxWord);

  for (const byte of bytes) {
    const lower = byte | 32;
    if (lower >= 97 && lower <= 122) {
      if (word.length < normalizedMaxWord) {
        word += String.fromCharCode(lower);
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
function renderText(result) {
  const rows = ["count word"];

  for (const entry of result.top) {
    rows.push(`${entry.count} ${entry.word}`);
  }
  rows.push(`total ${result.total}`);
  rows.push(`unique ${result.unique}`);
  return `${rows.join("\n")}\n`;
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
  const { values, positionals } = parseNodeArgs({
    args,
    allowPositionals: true,
    strict: true,
    options: {
      json: { type: "boolean", default: false },
      top: { type: "string", default: "10" },
      "max-word": { type: "string", default: String(maxWordLimit) },
      "bench-runs": { type: "string", default: "0" },
      "bench-warmups": { type: "string", default: "0" },
    },
  });
  const top = parseNumber(values.top);
  const [path] = positionals;
  if (path === undefined || positionals.length !== 1 || top === 0) {
    throw new Error(usage);
  }

  return {
    path,
    top,
    maxWord: normalizeMaxWord(parseNumber(values["max-word"])),
    benchRuns: parseNumber(values["bench-runs"]),
    benchWarmups: parseNumber(values["bench-warmups"]),
    json: values.json,
  };
}

/**
 * @param {string | undefined} value
 * @returns {number}
 */
function parseNumber(value) {
  if (value === undefined || !/^[0-9]+$/.test(value)) {
    throw new Error(usage);
  }
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed)) {
    throw new Error(usage);
  }
  return parsed;
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

try {
  const options = parseArgs(process.argv.slice(2));
  const bytes = readFileSync(options.path);
  if (options.benchRuns > 0) {
    process.stdout.write(renderBench(bytes, options));
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
