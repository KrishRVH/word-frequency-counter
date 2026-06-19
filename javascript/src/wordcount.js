#!/usr/bin/env node
// @ts-check

import { readFileSync } from "node:fs";

/**
 * @typedef {{ word: string, count: number }} Entry
 * @typedef {{ total: number, unique: number, top: Entry[] }} Result
 * @typedef {{ path: string, top: number, maxWord: number, json: boolean }} Options
 */

const defaultMaxWord = 64;
const maxWordLimit = 1024;
const minWord = 4;

/**
 * @param {Uint8Array} bytes
 * @param {number} limit
 * @param {number} maxWord
 * @returns {Result}
 */
export function countWords(bytes, limit = 10, maxWord = 1024) {
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
  let maxWord = 1024;
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
    } else if (arg.startsWith("-")) {
      throw new Error(
        "usage: node src/wordcount.js [--json] [--top N] [--max-word N] <file>",
      );
    } else if (path === undefined) {
      path = arg;
    } else {
      throw new Error("usage: node src/wordcount.js [--json] [--top N] <file>");
    }
  }

  if (
    path === undefined ||
    !Number.isInteger(top) ||
    top <= 0 ||
    !Number.isInteger(maxWord)
  ) {
    throw new Error(
      "usage: node src/wordcount.js [--json] [--top N] [--max-word N] <file>",
    );
  }

  return { path, top, maxWord: normalizeMaxWord(maxWord), json };
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
    return defaultMaxWord;
  }
  return Math.min(Math.max(value, minWord), maxWordLimit);
}

const invokedPath = process.argv[1];
if (invokedPath !== undefined && import.meta.url === `file://${invokedPath}`) {
  try {
    const options = parseArgs(process.argv.slice(2));
    const result = countWords(
      readFileSync(options.path),
      options.top,
      options.maxWord,
    );
    process.stdout.write(
      options.json ? `${JSON.stringify(result)}\n` : renderText(result),
    );
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    process.stderr.write(`wordcount_js: ${message}\n`);
    process.exitCode = 1;
  }
}
