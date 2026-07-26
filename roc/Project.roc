Project := [].{
	count_words = |bytes, limit, requested_max_word| {
		max_word = normalize_max_word(requested_max_word)
		var $counts = Dict.with_capacity(List.len(bytes) / 32)
		var $word = List.with_capacity(
			if max_word < 64 {
				max_word
			} else {
				64
			},
		)
		var $in_word = False
		var $total = 0.U64

		for byte in bytes {
			lower = U8.bitwise_or(byte, 0x20)
			if lower >= 97 and lower <= 122 {
				$in_word = True
				if List.len($word) < max_word {
					$word = List.append($word, lower)
				}
			} else if $in_word {
				$counts = increment($counts, Str.from_utf8_lossy($word))
				$word = List.clear($word)
				$in_word = False
				$total = $total + 1
			}
		}
		if $in_word {
			$counts = increment($counts, Str.from_utf8_lossy($word))
			$total = $total + 1
		}

		unique = Dict.len($counts)
		entries = Dict.to_list($counts)
			.sort_with(
				|(left_word, left_count), (right_word, right_count)|
					if left_count > right_count {
						LT
					} else if left_count < right_count {
						GT
					} else {
						compare_words(left_word, right_word)
					},
			)
			.take_first(limit)
		{ total: $total, unique, entries }
	}

	normalize_max_word : U64 -> U64
	normalize_max_word = |value|
		if value == 0 {
			64
		} else if value < 4 {
			4
		} else if value > 1024 {
			1024
		} else {
			value
		}

	increment = |counts, word|
		Dict.update(
			counts,
			word,
			|possible_count|
				match possible_count {
					Ok(count) => Ok(count + 1)
					Err(Missing) => Ok(1.U64)
				},
		)

	compare_words = |left, right|
		compare_bytes(Str.to_utf8(left), Str.to_utf8(right), 0)

	compare_bytes : List(U8), List(U8), U64 -> [EQ, GT, LT]
	compare_bytes = |left, right, index|
		if index == List.len(left) {
			if index == List.len(right) {
				EQ
			} else {
				LT
			}
		} else if index == List.len(right) {
			GT
		} else {
			match (List.get(left, index), List.get(right, index)) {
				(Ok(left_byte), Ok(right_byte)) =>
					if left_byte < right_byte {
						LT
					} else if left_byte > right_byte {
						GT
					} else {
						compare_bytes(left, right, index + 1)
					}
				_ => EQ
			}
		}
}
