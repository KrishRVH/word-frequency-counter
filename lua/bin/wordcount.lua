#!/usr/bin/env lua

local script_path = arg[0] or debug.getinfo(1, "S").source:sub(2)
package.path = script_path:gsub("/bin/wordcount%.lua$", "/src/?.lua")
  .. ";"
  .. package.path

local wordcount = require("wordcount")

local function usage()
  io.stderr:write(
    "usage: lua/bin/wordcount.lua [--json] [--top N] [--max-word N] <file>\n"
  )
  os.exit(2)
end

local function parse_number(value)
  local number = type(value) == "string"
    and value:match("^%d+$")
    and tonumber(value)
  if not number then
    usage()
  end
  return number
end

local function parse_next_number(args, index)
  local value = args[index + 1]
  if value == nil then
    usage()
  end
  return parse_number(value), index + 2
end

local option_handlers = {
  ["--json"] = function(_, index, options)
    options.json = true
    return index + 1
  end,
  ["--top"] = function(args, index, options)
    options.top, index = parse_next_number(args, index)
    return index
  end,
  ["--max-word"] = function(args, index, options)
    options.max_word, index = parse_next_number(args, index)
    return index
  end,
  ["--bench-runs"] = function(args, index, options)
    options.bench_runs, index = parse_next_number(args, index)
    return index
  end,
  ["--bench-warmups"] = function(args, index, options)
    options.bench_warmups, index = parse_next_number(args, index)
    return index
  end,
}

local function parse_prefixed_option(arg, options)
  local top = arg:match("^%-%-top=(.+)$")
  if top ~= nil then
    options.top = parse_number(top)
    return true
  end

  local max_word = arg:match("^%-%-max%-word=(.+)$")
  if max_word ~= nil then
    options.max_word = parse_number(max_word)
    return true
  end

  local bench_runs = arg:match("^%-%-bench%-runs=(.+)$")
  if bench_runs ~= nil then
    options.bench_runs = parse_number(bench_runs)
    return true
  end

  local bench_warmups = arg:match("^%-%-bench%-warmups=(.+)$")
  if bench_warmups ~= nil then
    options.bench_warmups = parse_number(bench_warmups)
    return true
  end

  return false
end

local function parse_arg(args, index, options)
  local arg = args[index]
  local handler = option_handlers[arg]
  if handler ~= nil then
    return handler(args, index, options)
  end

  if parse_prefixed_option(arg, options) then
    return index + 1
  end

  if arg:sub(1, 1) == "-" then
    usage()
  end

  if options.path == nil then
    options.path = arg
    return index + 1
  end

  usage()
end

local function parse_args(args)
  local options = {
    json = false,
    top = 10,
    max_word = 1024,
    bench_runs = 0,
    bench_warmups = 0,
    path = nil,
  }
  local index = 1

  while index <= #args do
    index = parse_arg(args, index, options)
  end

  if options.path == nil or options.top <= 0 then
    usage()
  end
  return options
end

local function read_file(path)
  local file, err = io.open(path, "rb")
  if file == nil then
    error(err)
  end
  local bytes = file:read("a")
  file:close()
  return bytes
end

local function render_json(result)
  local parts = {
    string.format(
      '{"total":%d,"unique":%d,"top":[',
      result.total,
      result.unique
    ),
  }
  for index, entry in ipairs(result.top) do
    parts[#parts + 1] = string.format(
      '%s{"word":"%s","count":%d}',
      index == 1 and "" or ",",
      entry.word,
      entry.count
    )
  end
  parts[#parts + 1] = "]}\n"
  return table.concat(parts)
end

local function render_text(result)
  local lines = { "count word" }
  for _, entry in ipairs(result.top) do
    lines[#lines + 1] = string.format("%d %s", entry.count, entry.word)
  end
  lines[#lines + 1] = string.format("total %d", result.total)
  lines[#lines + 1] = string.format("unique %d", result.unique)
  return table.concat(lines, "\n") .. "\n"
end

local checksum_offset = 2166136261
local checksum_prime = 16777619
local checksum_mod = 4294967296
local checksum_half = 65536

local function bxor(left, right)
  local value = 0
  local bit = 1

  while left > 0 or right > 0 do
    if left % 2 ~= right % 2 then
      value = value + bit
    end
    left = math.floor(left / 2)
    right = math.floor(right / 2)
    bit = bit * 2
  end

  return value
end

local function fnv_multiply(value)
  local low = value % checksum_half
  local high = math.floor(value / checksum_half)
  return (
    low * checksum_prime
    + (high * checksum_prime % checksum_half) * checksum_half
  ) % checksum_mod
end

local function mix_byte(checksum, value)
  return fnv_multiply(bxor(checksum, value))
end

local function mix_uint(checksum, value, bytes)
  for _ = 1, bytes do
    checksum = mix_byte(checksum, value % 256)
    value = math.floor(value / 256)
  end
  return checksum
end

local function mix_uint32(checksum, value)
  return mix_uint(checksum, value, 4)
end

local function mix_uint64(checksum, value)
  return mix_uint(checksum, value, 8)
end

local function checksum(result)
  local value = checksum_offset
  value = mix_uint64(value, result.total)
  value = mix_uint64(value, result.unique)
  for _, entry in ipairs(result.top) do
    for index = 1, #entry.word do
      value = mix_byte(value, string.byte(entry.word, index))
    end
    value = mix_uint64(value, entry.count)
  end
  return value
end

local function render_bench(bytes, options)
  for _ = 1, options.bench_warmups do
    checksum(wordcount.count_bytes(bytes, options.top, options.max_word))
  end

  local checksum_value = checksum_offset
  local started = os.clock()
  for _ = 1, options.bench_runs do
    checksum_value = mix_uint32(
      checksum_value,
      checksum(wordcount.count_bytes(bytes, options.top, options.max_word))
    )
  end
  local mean_ms = (os.clock() - started) * 1000 / options.bench_runs

  return string.format(
    '{"mean_ms":%.6f,"checksum":%d}\n',
    mean_ms,
    checksum_value
  )
end

local ok, err = pcall(function()
  local options = parse_args(arg)
  local bytes = read_file(options.path)
  if options.bench_runs > 0 then
    io.write(render_bench(bytes, options))
  else
    local result = wordcount.count_bytes(bytes, options.top, options.max_word)
    io.write(options.json and render_json(result) or render_text(result))
  end
end)

if not ok then
  io.stderr:write("wordcount_lua: " .. tostring(err) .. "\n")
  os.exit(1)
end
