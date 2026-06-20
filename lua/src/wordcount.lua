local M = {}

local ORACLE_DEFAULT_MAX_WORD = 64
local MAX_WORD = 1024
local MIN_WORD = 4

local function is_letter(byte)
  return (byte >= 65 and byte <= 90) or (byte >= 97 and byte <= 122)
end

local function lower_ascii(byte)
  if byte >= 65 and byte <= 90 then
    return byte + 32
  end
  return byte
end

local function normalize_max_word(value)
  if value == 0 then
    return ORACLE_DEFAULT_MAX_WORD
  end
  return math.min(math.max(value, MIN_WORD), MAX_WORD)
end

function M.count_bytes(bytes, top, max_word)
  max_word = normalize_max_word(max_word)

  local counts = {}
  local word = {}
  local stored = 0
  local total = 0
  local in_word = false

  for index = 1, #bytes do
    local byte = string.byte(bytes, index)
    if is_letter(byte) then
      in_word = true
      if stored < max_word then
        stored = stored + 1
        word[stored] = string.char(lower_ascii(byte))
      end
    elseif in_word then
      local key = table.concat(word, "", 1, stored)
      counts[key] = (counts[key] or 0) + 1
      total = total + 1
      word = {}
      stored = 0
      in_word = false
    end
  end

  if in_word then
    local key = table.concat(word, "", 1, stored)
    counts[key] = (counts[key] or 0) + 1
    total = total + 1
  end

  local entries = {}
  for key, count in pairs(counts) do
    entries[#entries + 1] = { word = key, count = count }
  end

  table.sort(entries, function(left, right)
    if left.count ~= right.count then
      return left.count > right.count
    end
    return left.word < right.word
  end)

  local limited = {}
  for index = 1, math.min(top, #entries) do
    limited[index] = entries[index]
  end

  return { total = total, unique = #entries, top = limited }
end

return M
