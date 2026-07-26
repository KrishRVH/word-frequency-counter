package main

import "core:fmt"
import "core:os"
import "core:slice"
import "core:strconv"
import "core:strings"
import "core:time"

CHECKSUM_OFFSET :: u32(2_166_136_261)
CHECKSUM_PRIME :: u32(16_777_619)
USAGE :: "usage: wordcount_odin [--json] [--top N] [--max-word N] <file>"

Entry :: struct {
	word:  string,
	count: u64,
}

Result :: struct {
	total:   u64,
	unique:  int,
	entries: [dynamic]Entry,
}

Options :: struct {
	path:          string,
	top:           int,
	max_word:      int,
	bench_runs:    int,
	bench_warmups: int,
	json:          bool,
}

main :: proc() {
	options, ok := parse_args(os.args[1:])
	if !ok {
		fmt.eprintln(USAGE)
		os.exit(1)
	}

	data, read_error := os.read_entire_file(options.path, context.allocator)
	if read_error != nil {
		fmt.eprintf("wordcount_odin: cannot read %s\n", options.path)
		os.exit(1)
	}
	defer delete(data)

	if options.bench_runs > 0 {
		render_bench(data, options)
		return
	}

	result := count_words(data, options.top, options.max_word)
	defer destroy_result(&result)
	if options.json {
		render_json(result)
	} else {
		render_text(result)
	}
}

count_words :: proc(data: []byte, limit, requested_max_word: int) -> Result {
	counts := make(map[string]u64, len(data) / 32)
	max_word := normalize_max_word(requested_max_word)
	total: u64
	cursor := 0
	word_buffer: [1024]byte

	for cursor < len(data) {
		for cursor < len(data) && !is_letter(data[cursor]) {
			cursor += 1
		}

		stored := 0
		for cursor < len(data) && is_letter(data[cursor]) {
			if stored < max_word {
				word_buffer[stored] = data[cursor] | 0x20
				stored += 1
			}
			cursor += 1
		}
		if stored == 0 {
			continue
		}

		word := string(word_buffer[:stored])
		if count, found := counts[word]; found {
			counts[word] = count + 1
		} else {
			counts[strings.clone(word)] = 1
		}
		total += 1
	}

	entries := make([dynamic]Entry, 0, len(counts))
	for word, count in counts {
		append(&entries, Entry{word, count})
	}
	unique := len(entries)
	delete(counts)
	slice.sort_by(entries[:], entry_less)
	if len(entries) > limit {
		for entry in entries[limit:] {
			delete(entry.word)
		}
		resize(&entries, limit)
	}
	return Result{total, unique, entries}
}

entry_less :: proc(left, right: Entry) -> bool {
	if left.count != right.count {
		return left.count > right.count
	}
	return strings.compare(left.word, right.word) < 0
}

destroy_result :: proc(result: ^Result) {
	for entry in result.entries {
		delete(entry.word)
	}
	delete(result.entries)
}

normalize_max_word :: proc(value: int) -> int {
	if value == 0 {
		return 64
	}
	return clamp(value, 4, 1024)
}

is_letter :: proc(value: byte) -> bool {
	lower := value | 0x20
	return lower >= 'a' && lower <= 'z'
}

checksum :: proc(result: Result) -> u32 {
	value := mix_integer(CHECKSUM_OFFSET, result.total, 8)
	value = mix_integer(value, u64(result.unique), 8)
	for entry in result.entries {
		for byte in transmute([]byte)entry.word {
			value = mix_byte(value, byte)
		}
		value = mix_integer(value, entry.count, 8)
	}
	return value
}

mix_byte :: proc(value: u32, byte: u8) -> u32 {
	return (value ~ u32(byte)) * CHECKSUM_PRIME
}

mix_integer :: proc(value: u32, number: u64, width: int) -> u32 {
	checksum_value := value
	remaining := number
	for _ in 0 ..< width {
		checksum_value = mix_byte(checksum_value, u8(remaining & 0xff))
		remaining >>= 8
	}
	return checksum_value
}

render_bench :: proc(data: []byte, options: Options) {
	for _ in 0 ..< options.bench_warmups {
		result := count_words(data, options.top, options.max_word)
		_ = checksum(result)
		destroy_result(&result)
	}

	value := CHECKSUM_OFFSET
	started := time.tick_now()
	for _ in 0 ..< options.bench_runs {
		result := count_words(data, options.top, options.max_word)
		value = mix_integer(value, u64(checksum(result)), 4)
		destroy_result(&result)
	}
	mean_ms := time.duration_milliseconds(time.tick_since(started)) / f64(options.bench_runs)
	fmt.printf("{{\"mean_ms\":%.6f,\"checksum\":%d}}\n", mean_ms, value)
}

render_json :: proc(result: Result) {
	fmt.printf("{{\"total\":%d,\"unique\":%d,\"top\":[", result.total, result.unique)
	for entry, index in result.entries {
		if index > 0 {
			fmt.print(",")
		}
		fmt.printf("{{\"word\":\"%s\",\"count\":%d}}", entry.word, entry.count)
	}
	fmt.println("]}")
}

render_text :: proc(result: Result) {
	fmt.println("count word")
	for entry in result.entries {
		fmt.printf("%d %s\n", entry.count, entry.word)
	}
	fmt.printf("total %d\nunique %d\n", result.total, result.unique)
}

parse_args :: proc(args: []string) -> (Options, bool) {
	options := Options {
		top      = 10,
		max_word = 1024,
	}
	index := 0

	for index < len(args) {
		arg := args[index]
		index += 1
		switch arg {
		case "--json":
			options.json = true
		case "--top", "--max-word", "--bench-runs", "--bench-warmups":
			if index == len(args) || !set_number(&options, arg, args[index]) {
				return {}, false
			}
			index += 1
		case:
			if strings.has_prefix(arg, "--top=") {
				if !set_number(&options, "--top", arg[len("--top="):]) {
					return {}, false
				}
			} else if strings.has_prefix(arg, "--max-word=") {
				if !set_number(&options, "--max-word", arg[len("--max-word="):]) {
					return {}, false
				}
			} else if strings.has_prefix(arg, "--bench-runs=") {
				if !set_number(&options, "--bench-runs", arg[len("--bench-runs="):]) {
					return {}, false
				}
			} else if strings.has_prefix(arg, "--bench-warmups=") {
				if !set_number(&options, "--bench-warmups", arg[len("--bench-warmups="):]) {
					return {}, false
				}
			} else if strings.has_prefix(arg, "-") || options.path != "" {
				return {}, false
			} else {
				options.path = arg
			}
		}
	}

	options.max_word = normalize_max_word(options.max_word)
	return options, options.path != "" && options.top > 0
}

set_number :: proc(options: ^Options, name, text: string) -> bool {
	value, ok := strconv.parse_uint(text, 10)
	if !ok || value > uint(max(int)) {
		return false
	}
	switch name {
	case "--top":
		options.top = int(value)
	case "--max-word":
		options.max_word = int(value)
	case "--bench-runs":
		options.bench_runs = int(value)
	case:
		options.bench_warmups = int(value)
	}
	return true
}
