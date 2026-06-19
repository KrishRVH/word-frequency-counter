#!/usr/bin/env lua

package.path = debug
  .getinfo(1, "S").source
  :sub(2)
  :gsub("/bin/wordcount%.lua$", "/src/?.lua") .. ";" .. package.path

local wordcount = require("wordcount")

local DEFAULT_MAX_WORD = 64
local MAX_WORD = 1024
local MIN_WORD = 4

local function usage()
  io.stderr:write(
    "usage: lua/bin/wordcount.lua [--json] [--top N] [--max-word N] <file>\n"
  )
  os.exit(2)
end

local function parse_number(value)
  if type(value) ~= "string" or value:match("^%d+$") == nil then
    usage()
  end
  local number = tonumber(value)
  if number == nil then
    usage()
  end
  return number
end

local function normalize_max_word(value)
  if value == 0 then
    return DEFAULT_MAX_WORD
  end
  return math.min(math.max(value, MIN_WORD), MAX_WORD)
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
  local options = { json = false, top = 10, max_word = 1024, path = nil }
  local index = 1

  while index <= #args do
    index = parse_arg(args, index, options)
  end

  if options.path == nil or options.top <= 0 then
    usage()
  end
  options.max_word = normalize_max_word(options.max_word)

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

local ok, err = pcall(function()
  local options = parse_args(arg)
  local result = wordcount.count_bytes(
    read_file(options.path),
    options.top,
    options.max_word
  )
  io.write(options.json and render_json(result) or render_text(result))
end)

if not ok then
  io.stderr:write("wordcount_lua: " .. tostring(err) .. "\n")
  os.exit(1)
end
