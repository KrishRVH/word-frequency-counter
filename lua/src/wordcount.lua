local M = {}

local ORACLE_DEFAULT_MAX_WORD = 64
local MAX_WORD = 1024
local MIN_WORD = 4

local function normalize_max_word(value)
  if value == 0 then
    return ORACLE_DEFAULT_MAX_WORD
  end
  return math.min(math.max(value, MIN_WORD), MAX_WORD)
end

local function ranks_before(left, right)
  if left.count ~= right.count then
    return left.count > right.count
  end
  return left.word < right.word
end

local function rank_counts(counts, top)
  local entries = {}
  for word, count in pairs(counts) do
    entries[#entries + 1] = { word = word, count = count }
  end
  table.sort(entries, ranks_before)

  local limited = {}
  for index = 1, math.min(top, #entries) do
    limited[index] = entries[index]
  end
  return limited, #entries
end

function M.count_bytes(bytes, top, max_word)
  max_word = normalize_max_word(max_word)

  local counts = {}
  local word = {}
  local stored = 0
  local total = 0

  for index = 1, #bytes do
    local byte = string.byte(bytes, index)
    local lower = byte | 32
    if lower >= 97 and lower <= 122 then
      if stored < max_word then
        stored = stored + 1
        word[stored] = string.char(lower)
      end
    elseif stored > 0 then
      local key = table.concat(word, "", 1, stored)
      counts[key] = (counts[key] or 0) + 1
      total = total + 1
      word = {}
      stored = 0
    end
  end

  if stored > 0 then
    local key = table.concat(word, "", 1, stored)
    counts[key] = (counts[key] or 0) + 1
    total = total + 1
  end

  local entries, unique = rank_counts(counts, top)
  return { total = total, unique = unique, top = entries }
end

return M
