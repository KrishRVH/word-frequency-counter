-- Luacheck strict baseline.
--
-- Keep this project-specific copy aligned with ~/dev/personal/standards/Lua.

std = "lua54"

allow_defined = false
allow_defined_top = false
module = false

unused = true
unused_args = true
unused_secondaries = true
redefined = true
self = true

ignore = {
  "211/^_",
  "212/^_",
  "213/^_",
}

max_line_length = 80
max_cyclomatic_complexity = 10

exclude_files = {
  ".lua_modules/**",
  "lua_modules/**",
  "luarocks_modules/**",
  "vendor/**",
  "third_party/**",
  "build/**",
  "dist/**",
  "coverage/**",
  "*.min.lua",
}
