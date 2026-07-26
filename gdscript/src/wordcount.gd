extends SceneTree

const CHECKSUM_OFFSET := 2166136261
const CHECKSUM_PRIME := 16777619
const USAGE := "usage: wordcount.gd [--json] [--top N] [--max-word N] <file>"


class Options:
	var path: String
	var top: int = 10
	var max_word: int = 1024
	var bench_runs: int = 0
	var bench_warmups: int = 0
	var json: bool = false


class Result:
	var total: int
	var counts: Dictionary[String, int]
	var words: Array[String]

	func _init(result_total: int, result_counts: Dictionary[String, int]) -> void:
		total = result_total
		counts = result_counts
		words.assign(counts.keys())
		words.sort_custom(
			func(left: String, right: String) -> bool:
				var left_count: int = counts[left]
				var right_count: int = counts[right]
				return left_count > right_count or (left_count == right_count and left < right)
		)


func _init() -> void:
	var options := parse_args(OS.get_cmdline_user_args())
	if options == null:
		quit(1)
		return
	var file := FileAccess.open(options.path, FileAccess.READ)
	if file == null:
		printerr("wordcount.gd: cannot read %s" % options.path)
		quit(1)
		return

	var bytes := file.get_buffer(file.get_length())
	file.close()
	if options.bench_runs > 0:
		print(render_bench(bytes, options))
	else:
		var result := count_words(bytes, options.max_word)
		print(
			render_json(result, options.top) if options.json else render_text(result, options.top)
		)
	quit()


func count_words(bytes: PackedByteArray, requested_max_word: int) -> Result:
	var counts: Dictionary[String, int] = {}
	var word := PackedByteArray()
	var max_word := normalize_max_word(requested_max_word)
	var in_word := false
	var total := 0

	for byte: int in bytes:
		var lower := byte | 0x20
		if lower >= 97 and lower <= 122:
			in_word = true
			if word.size() < max_word:
				word.append(lower)
		elif in_word:
			var text := word.get_string_from_ascii()
			counts[text] = counts[text] + 1 if counts.has(text) else 1
			total += 1
			word.clear()
			in_word = false

	if in_word:
		var text := word.get_string_from_ascii()
		counts[text] = counts[text] + 1 if counts.has(text) else 1
		total += 1

	return Result.new(total, counts)


func normalize_max_word(value: int) -> int:
	if value == 0:
		return 64
	return clampi(value, 4, 1024)


func checksum(result: Result, limit: int) -> int:
	var value := mix_integer(CHECKSUM_OFFSET, result.total, 8)
	value = mix_integer(value, result.counts.size(), 8)
	for index in mini(limit, result.words.size()):
		var word: String = result.words[index]
		for byte: int in word.to_ascii_buffer():
			value = mix_byte(value, byte)
		value = mix_integer(value, result.counts[word], 8)
	return value


func mix_byte(value: int, byte: int) -> int:
	return ((value ^ byte) * CHECKSUM_PRIME) & 0xFFFFFFFF


func mix_integer(checksum_value: int, number: int, width: int) -> int:
	for _index in width:
		checksum_value = mix_byte(checksum_value, number & 0xFF)
		number >>= 8
	return checksum_value


func render_bench(bytes: PackedByteArray, options: Options) -> String:
	for _index in options.bench_warmups:
		checksum(count_words(bytes, options.max_word), options.top)

	var value := CHECKSUM_OFFSET
	var started := Time.get_ticks_usec()
	for _index in options.bench_runs:
		value = mix_integer(value, checksum(count_words(bytes, options.max_word), options.top), 4)
	var mean_ms := (Time.get_ticks_usec() - started) / 1000.0 / options.bench_runs
	return '{"mean_ms":%.6f,"checksum":%d}' % [mean_ms, value]


func render_json(result: Result, limit: int) -> String:
	var entries: Array[String] = []
	for index in mini(limit, result.words.size()):
		var word: String = result.words[index]
		entries.append('{"word":"%s","count":%d}' % [word, result.counts[word]])
	return (
		'{"total":%d,"unique":%d,"top":[%s]}'
		% [result.total, result.counts.size(), ",".join(entries)]
	)


func render_text(result: Result, limit: int) -> String:
	var lines: Array[String] = ["count word"]
	for index in mini(limit, result.words.size()):
		var word: String = result.words[index]
		lines.append("%d %s" % [result.counts[word], word])
	lines.append("total %d" % result.total)
	lines.append("unique %d" % result.counts.size())
	return "\n".join(lines)


func parse_args(args: PackedStringArray) -> Options:
	var options := Options.new()
	var index := 0
	var valid := true

	while index < args.size() and valid:
		var arg := args[index]
		index += 1
		if arg == "--json":
			options.json = true
		elif arg in ["--top", "--max-word", "--bench-runs", "--bench-warmups"]:
			if index == args.size():
				printerr(USAGE)
				valid = false
			else:
				valid = set_number(options, arg, args[index])
				index += 1
		elif arg.begins_with("--top="):
			valid = set_number(options, "--top", arg.trim_prefix("--top="))
		elif arg.begins_with("--max-word="):
			valid = set_number(options, "--max-word", arg.trim_prefix("--max-word="))
		elif arg.begins_with("--bench-runs="):
			valid = set_number(options, "--bench-runs", arg.trim_prefix("--bench-runs="))
		elif arg.begins_with("--bench-warmups="):
			valid = set_number(options, "--bench-warmups", arg.trim_prefix("--bench-warmups="))
		elif arg.begins_with("-") or not options.path.is_empty():
			printerr(USAGE)
			valid = false
		else:
			options.path = arg

	if not valid:
		return null
	if options.path.is_empty() or options.top == 0:
		printerr(USAGE)
		return null
	return options


func set_number(options: Options, name: String, text: String) -> bool:
	if not text.is_valid_int() or text.begins_with("-"):
		printerr("wordcount.gd: %s must be a number" % name)
		return false
	var value := text.to_int()
	match name:
		"--top":
			options.top = value
		"--max-word":
			options.max_word = value
		"--bench-runs":
			options.bench_runs = value
		_:
			options.bench_warmups = value
	return true
