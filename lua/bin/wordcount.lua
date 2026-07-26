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

local number_options = {
  ["--top"] = "top",
  ["--max-word"] = "max_word",
  ["--bench-runs"] = "bench_runs",
  ["--bench-warmups"] = "bench_warmups",
}

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
    local argument = args[index]
    index = index + 1

    if argument == "--json" then
      options.json = true
    else
      local name, inline_value = argument:match("^(%-%-[^=]+)=(.*)$")
      name = name or argument
      local field = number_options[name]
      if field ~= nil then
        local value = inline_value
        if value == nil then
          value = args[index]
          index = index + 1
        end
        options[field] = parse_number(value)
      elseif argument:sub(1, 1) == "-" or options.path ~= nil then
        usage()
      else
        options.path = argument
      end
    end
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
  local bytes, read_err = file:read("a")
  local closed, close_err = file:close()
  if bytes == nil then
    error(read_err)
  end
  if not closed then
    error(close_err)
  end
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
local checksum_mask = 0xffffffff

local function mix_byte(checksum, value)
  return ((checksum ~ value) * checksum_prime) & checksum_mask
end

local function mix_uint(checksum, value, bytes)
  for _ = 1, bytes do
    checksum = mix_byte(checksum, value & 0xff)
    value = value >> 8
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
