//! Case-insensitive ASCII word frequencies.

const std = @import("std");
const Io = std.Io;
const Allocator = std.mem.Allocator;

const usage = "usage: wordcount_zig [--json] [--top N] [--max-word N] <file>";
const default_max_word: usize = 64;
const estimated_bytes_per_unique_word: usize = 32;
const max_word_limit: usize = 1024;
const min_word: usize = 4;
const checksum_offset: u32 = 2166136261;
const checksum_prime: u32 = 16777619;

const Entry = struct {
    word: []const u8,
    count: u64,

    fn lessThan(_: void, a: Entry, b: Entry) bool {
        if (a.count != b.count) return a.count > b.count;
        return std.mem.lessThan(u8, a.word, b.word);
    }
};

const Result = struct {
    total: u64,
    unique: usize,
    top: []const Entry,
};

const Options = struct {
    path: []const u8 = "",
    top: usize = 10,
    max_word: usize = max_word_limit,
    bench_runs: usize = 0,
    bench_warmups: usize = 0,
    json: bool = false,
};

pub fn main(init: std.process.Init) !void {
    const gpa = init.gpa;
    const io = init.io;
    const args = try init.minimal.args.toSlice(init.arena.allocator());
    const options = parseArgs(args[1..]) catch {
        var buffer: [256]u8 = undefined;
        var stderr_writer = Io.File.stderr().writer(io, &buffer);
        const stderr = &stderr_writer.interface;
        try stderr.print("{s}\n", .{usage});
        try stderr.flush();
        std.process.exit(2);
    };

    const bytes = Io.Dir.cwd().readFileAlloc(io, options.path, gpa, .unlimited) catch |err|
        std.process.fatal("{s}: {t}", .{ options.path, err });
    defer gpa.free(bytes);

    var buffer: [4096]u8 = undefined;
    var stdout_writer = Io.File.stdout().writer(io, &buffer);
    const stdout = &stdout_writer.interface;

    if (options.bench_runs > 0) {
        try renderBench(stdout, io, gpa, bytes, options);
    } else {
        var arena = std.heap.ArenaAllocator.init(gpa);
        defer arena.deinit();

        const result = try countBytes(arena.allocator(), bytes, options.top, options.max_word);
        if (options.json) {
            try stdout.print("{f}\n", .{std.json.fmt(result, .{})});
        } else {
            try renderText(stdout, result);
        }
    }
    try stdout.flush();
}

fn parseArgs(args: []const []const u8) !Options {
    var options: Options = .{};
    var seen_path = false;
    var index: usize = 0;

    while (index < args.len) : (index += 1) {
        const arg = args[index];
        if (std.mem.eql(u8, arg, "--json")) {
            options.json = true;
        } else if (std.mem.eql(u8, arg, "--top")) {
            index += 1;
            if (index == args.len) return error.Usage;
            options.top = std.fmt.parseUnsigned(usize, args[index], 10) catch return error.Usage;
        } else if (std.mem.startsWith(u8, arg, "--top=")) {
            options.top = std.fmt.parseUnsigned(usize, arg["--top=".len..], 10) catch return error.Usage;
        } else if (std.mem.eql(u8, arg, "--max-word")) {
            index += 1;
            if (index == args.len) return error.Usage;
            options.max_word = std.fmt.parseUnsigned(usize, args[index], 10) catch return error.Usage;
        } else if (std.mem.startsWith(u8, arg, "--max-word=")) {
            options.max_word = std.fmt.parseUnsigned(usize, arg["--max-word=".len..], 10) catch return error.Usage;
        } else if (std.mem.eql(u8, arg, "--bench-runs")) {
            index += 1;
            if (index == args.len) return error.Usage;
            options.bench_runs = std.fmt.parseUnsigned(usize, args[index], 10) catch return error.Usage;
        } else if (std.mem.startsWith(u8, arg, "--bench-runs=")) {
            options.bench_runs = std.fmt.parseUnsigned(usize, arg["--bench-runs=".len..], 10) catch return error.Usage;
        } else if (std.mem.eql(u8, arg, "--bench-warmups")) {
            index += 1;
            if (index == args.len) return error.Usage;
            options.bench_warmups = std.fmt.parseUnsigned(usize, args[index], 10) catch return error.Usage;
        } else if (std.mem.startsWith(u8, arg, "--bench-warmups=")) {
            options.bench_warmups = std.fmt.parseUnsigned(usize, arg["--bench-warmups=".len..], 10) catch return error.Usage;
        } else if (std.mem.startsWith(u8, arg, "-")) {
            return error.Usage;
        } else if (!seen_path) {
            options.path = arg;
            seen_path = true;
        } else {
            return error.Usage;
        }
    }

    if (!seen_path or options.top == 0) return error.Usage;
    return options;
}

/// Mutates bytes by lowercasing retained words. The result's entries use
/// allocator-backed storage and its words alias bytes, so both must remain alive
/// while the result is used; bytes may safely be counted again.
fn countBytes(allocator: Allocator, bytes: []u8, top: usize, max_word: usize) !Result {
    var counts: std.StringHashMapUnmanaged(u64) = .empty;
    defer counts.deinit(allocator);
    try counts.ensureTotalCapacity(allocator, estimatedUniqueWords(bytes));

    var total: u64 = 0;
    var start: usize = 0;
    var length: usize = 0;
    const word_limit = normalizeMaxWord(max_word);

    for (bytes, 0..) |byte, index| {
        if (std.ascii.isAlphabetic(byte)) {
            if (length < word_limit) {
                if (length == 0) start = index;
                bytes[index] = std.ascii.toLower(byte);
                length += 1;
            }
        } else if (length > 0) {
            try bump(allocator, &counts, bytes[start .. start + length], &total);
            length = 0;
        }
    }
    if (length > 0) try bump(allocator, &counts, bytes[start .. start + length], &total);

    const entries = try allocator.alloc(Entry, counts.count());
    var iterator = counts.iterator();
    var index: usize = 0;
    while (iterator.next()) |kv| : (index += 1) {
        entries[index] = .{ .word = kv.key_ptr.*, .count = kv.value_ptr.* };
    }
    std.sort.pdq(Entry, entries, {}, Entry.lessThan);

    return .{
        .total = total,
        .unique = entries.len,
        .top = entries[0..@min(top, entries.len)],
    };
}

fn bump(
    allocator: Allocator,
    counts: *std.StringHashMapUnmanaged(u64),
    word: []const u8,
    total: *u64,
) !void {
    const gop = try counts.getOrPut(allocator, word);
    gop.value_ptr.* = if (gop.found_existing) gop.value_ptr.* + 1 else 1;
    total.* += 1;
}

fn estimatedUniqueWords(bytes: []const u8) u32 {
    const estimate = bytes.len / estimated_bytes_per_unique_word;
    return @intCast(@min(estimate, 1 << 20));
}

fn normalizeMaxWord(value: usize) usize {
    if (value == 0) return default_max_word;
    return std.math.clamp(value, min_word, max_word_limit);
}

fn countChecksum(
    allocator: Allocator,
    bytes: []u8,
    top: usize,
    max_word: usize,
) !u32 {
    var arena = std.heap.ArenaAllocator.init(allocator);
    defer arena.deinit();

    const result = try countBytes(arena.allocator(), bytes, top, max_word);
    var value = checksum_offset;
    value = mixU64(value, result.total);
    value = mixU64(value, @intCast(result.unique));
    for (result.top) |entry| {
        for (entry.word) |byte| value = mixByte(value, byte);
        value = mixU64(value, entry.count);
    }
    return value;
}

fn mixByte(checksum: u32, value: u8) u32 {
    return (checksum ^ @as(u32, value)) *% checksum_prime;
}

fn mixU32(checksum: u32, value: u32) u32 {
    var mixed = checksum;
    var remaining = value;
    for (0..4) |_| {
        mixed = mixByte(mixed, @intCast(remaining & 0xff));
        remaining >>= 8;
    }
    return mixed;
}

fn mixU64(checksum: u32, value: u64) u32 {
    var mixed = checksum;
    var remaining = value;
    for (0..8) |_| {
        mixed = mixByte(mixed, @intCast(remaining & 0xff));
        remaining >>= 8;
    }
    return mixed;
}

fn renderBench(
    writer: anytype,
    io: Io,
    allocator: Allocator,
    bytes: []u8,
    options: Options,
) !void {
    for (0..options.bench_warmups) |_| {
        _ = try countChecksum(allocator, bytes, options.top, options.max_word);
    }

    var checksum = checksum_offset;
    const started = Io.Clock.awake.now(io).nanoseconds;
    for (0..options.bench_runs) |_| {
        checksum = mixU32(checksum, try countChecksum(allocator, bytes, options.top, options.max_word));
    }
    const elapsed = Io.Clock.awake.now(io).nanoseconds - started;
    const mean_ms = @as(f64, @floatFromInt(elapsed)) / 1_000_000.0 / @as(f64, @floatFromInt(options.bench_runs));

    try writer.print("{{\"mean_ms\":{d:.6},\"checksum\":{}}}\n", .{ mean_ms, checksum });
}

fn renderText(writer: anytype, result: Result) !void {
    try writer.writeAll("count word\n");
    for (result.top) |entry| try writer.print("{} {s}\n", .{ entry.count, entry.word });
    try writer.print("total {}\nunique {}\n", .{ result.total, result.unique });
}
