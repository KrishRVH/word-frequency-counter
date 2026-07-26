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
  ".lua-language-server/**",
  ".lua_modules/**",
  "lua_modules/**",
  "luarocks_modules/**",
  "third_party/**",
  "vendor/**",
  "build/**",
  "coverage/**",
  "dist/**",
  "*.min.lua",
}
