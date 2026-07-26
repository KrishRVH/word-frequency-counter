"""Count ASCII words in a file."""

import sys
from dataclasses import dataclass
from pathlib import Path
from time import perf_counter_ns

CHECKSUM_OFFSET = 2_166_136_261
CHECKSUM_PRIME = 16_777_619
CHECKSUM_MASK = 0xFFFF_FFFF
USAGE = "usage: wordcount.py [--json] [--top N] [--max-word N] <file>"

type Entry = tuple[bytes, int]
type Result = tuple[int, int, list[Entry]]


@dataclass(frozen=True, slots=True)
class Options:
    path: Path
    top: int = 10
    max_word: int = 1024
    bench_runs: int = 0
    bench_warmups: int = 0
    json: bool = False


def count_words(data: bytes, limit: int, requested_max_word: int) -> Result:
    """Count and rank ASCII words in data."""
    max_word = normalize_max_word(requested_max_word)
    counts: dict[bytes, int] = {}
    total = 0
    cursor = 0
    data_length = len(data)

    while cursor < data_length:
        while cursor < data_length and not 97 <= (data[cursor] | 0x20) <= 122:
            cursor += 1
        start = cursor
        while cursor < data_length and 97 <= (data[cursor] | 0x20) <= 122:
            cursor += 1
        if start == cursor:
            continue

        word = data[start : min(cursor, start + max_word)].lower()
        counts[word] = counts.get(word, 0) + 1
        total += 1

    entries = sorted(counts.items(), key=lambda item: (-item[1], item[0]))
    return total, len(entries), entries[:limit]


def normalize_max_word(value: int) -> int:
    if value == 0:
        return 64
    return min(1024, max(4, value))


def checksum(result: Result) -> int:
    total, unique, entries = result
    value = mix_integer(CHECKSUM_OFFSET, total, 8)
    value = mix_integer(value, unique, 8)
    for word, count in entries:
        for byte in word:
            value = mix_byte(value, byte)
        value = mix_integer(value, count, 8)
    return value


def mix_byte(value: int, byte: int) -> int:
    return ((value ^ byte) * CHECKSUM_PRIME) & CHECKSUM_MASK


def mix_integer(checksum_value: int, value: int, width: int) -> int:
    for _ in range(width):
        checksum_value = mix_byte(checksum_value, value & 0xFF)
        value >>= 8
    return checksum_value


def bench(data: bytes, options: Options) -> str:
    for _ in range(options.bench_warmups):
        checksum(count_words(data, options.top, options.max_word))

    value = CHECKSUM_OFFSET
    started = perf_counter_ns()
    for _ in range(options.bench_runs):
        value = mix_integer(
            value,
            checksum(count_words(data, options.top, options.max_word)),
            4,
        )
    mean_ms = (perf_counter_ns() - started) / options.bench_runs / 1_000_000
    return f'{{"mean_ms":{mean_ms:.6f},"checksum":{value}}}'


def render_json(result: Result) -> str:
    total, unique, entries = result
    top = ",".join(
        f'{{"word":"{word.decode("ascii")}","count":{count}}}' for word, count in entries
    )
    return f'{{"total":{total},"unique":{unique},"top":[{top}]}}'


def render_text(result: Result) -> str:
    total, unique, entries = result
    lines = ["count word", *(f"{count} {word.decode('ascii')}" for word, count in entries)]
    lines.extend((f"total {total}", f"unique {unique}"))
    return "\n".join(lines)


def parse_args(args: list[str]) -> Options:
    path: Path | None = None
    top = 10
    max_word = 1024
    bench_runs = 0
    bench_warmups = 0
    json_output = False
    index = 0

    while index < len(args):
        arg = args[index]
        index += 1
        if arg == "--json":
            json_output = True
        elif arg in {"--top", "--max-word", "--bench-runs", "--bench-warmups"}:
            if index == len(args):
                raise ValueError(USAGE)
            value = parse_number(args[index], arg)
            index += 1
            if arg == "--top":
                top = value
            elif arg == "--max-word":
                max_word = value
            elif arg == "--bench-runs":
                bench_runs = value
            else:
                bench_warmups = value
        elif arg.startswith("--top="):
            top = parse_number(arg.removeprefix("--top="), "--top")
        elif arg.startswith("--max-word="):
            max_word = parse_number(arg.removeprefix("--max-word="), "--max-word")
        elif arg.startswith("--bench-runs="):
            bench_runs = parse_number(arg.removeprefix("--bench-runs="), "--bench-runs")
        elif arg.startswith("--bench-warmups="):
            bench_warmups = parse_number(arg.removeprefix("--bench-warmups="), "--bench-warmups")
        elif arg.startswith("-") or path is not None:
            raise ValueError(USAGE)
        else:
            path = Path(arg)

    if path is None or top == 0:
        raise ValueError(USAGE)
    return Options(path, top, max_word, bench_runs, bench_warmups, json_output)


def parse_number(value: str, name: str) -> int:
    if not value or not value.isascii() or not value.isdecimal():
        raise ValueError(f"wordcount.py: {name} must be a number")
    return int(value)


def main() -> int:
    try:
        options = parse_args(sys.argv[1:])
        data = options.path.read_bytes()
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1

    if options.bench_runs > 0:
        print(bench(data, options))
    else:
        result = count_words(data, options.top, options.max_word)
        print(render_json(result) if options.json else render_text(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
