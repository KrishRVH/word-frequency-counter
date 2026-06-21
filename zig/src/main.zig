const std = @import("std");
const Io = std.Io;

const default_max_word: usize = 64;
const estimated_bytes_per_unique_word: usize = 32;
const max_word_limit: usize = 1024;
const min_word: usize = 4;

const Entry = struct {
    word: []const u8,
    count: u64,
};

const Result = struct {
    total: u64,
    unique: usize,
    top: []Entry,
};

const Options = struct {
    path: []const u8,
    top: usize = 10,
    max_word: usize = 1024,
    bench_runs: usize = 0,
    bench_warmups: usize = 0,
    json: bool = false,
};

pub fn main(init: std.process.Init) !void {
    const allocator = init.gpa;
    const process_arena = init.arena.allocator();
    const io = init.io;
    const args = try init.minimal.args.toSlice(process_arena);

    const options = parseArgs(args[1..]) catch {
        var stderr_buffer: [256]u8 = undefined;
        var stderr_writer = Io.File.stderr().writer(io, &stderr_buffer);
        const stderr = &stderr_writer.interface;
        try stderr.writeAll(
            "usage: wordcount_zig [--json] [--top N] [--max-word N] <file>\n",
        );
        try stderr.flush();
        std.process.exit(2);
    };

    const bytes = try Io.Dir.cwd().readFileAlloc(io, options.path, allocator, .unlimited);
    defer allocator.free(bytes);

    var stdout_buffer: [4096]u8 = undefined;
    var stdout_writer = Io.File.stdout().writer(io, &stdout_buffer);
    const stdout = &stdout_writer.interface;

    if (options.bench_runs > 0) {
        try renderBench(stdout, io, allocator, bytes, options);
        try stdout.flush();
        return;
    }

    var arena = std.heap.ArenaAllocator.init(allocator);
    defer arena.deinit();

    const result = try countBytes(arena.allocator(), bytes, options.top, options.max_word);
    if (options.json) {
        try renderJson(stdout, result);
    } else {
        try renderText(stdout, result);
    }
    try stdout.flush();
}

fn parseArgs(args: []const []const u8) !Options {
    var options = Options{ .path = "" };
    var has_path = false;
    var index: usize = 0;

    while (index < args.len) : (index += 1) {
        const arg = args[index];
        if (std.mem.eql(u8, arg, "--json")) {
            options.json = true;
        } else if (std.mem.eql(u8, arg, "--top")) {
            index += 1;
            if (index >= args.len) return error.Usage;
            options.top = try std.fmt.parseUnsigned(usize, args[index], 10);
        } else if (std.mem.startsWith(u8, arg, "--top=")) {
            options.top = try std.fmt.parseUnsigned(usize, arg["--top=".len..], 10);
        } else if (std.mem.eql(u8, arg, "--max-word")) {
            index += 1;
            if (index >= args.len) return error.Usage;
            options.max_word = try std.fmt.parseUnsigned(usize, args[index], 10);
        } else if (std.mem.startsWith(u8, arg, "--max-word=")) {
            options.max_word = try std.fmt.parseUnsigned(usize, arg["--max-word=".len..], 10);
        } else if (std.mem.eql(u8, arg, "--bench-runs")) {
            index += 1;
            if (index >= args.len) return error.Usage;
            options.bench_runs = try std.fmt.parseUnsigned(usize, args[index], 10);
        } else if (std.mem.startsWith(u8, arg, "--bench-runs=")) {
            options.bench_runs = try std.fmt.parseUnsigned(usize, arg["--bench-runs=".len..], 10);
        } else if (std.mem.eql(u8, arg, "--bench-warmups")) {
            index += 1;
            if (index >= args.len) return error.Usage;
            options.bench_warmups = try std.fmt.parseUnsigned(usize, args[index], 10);
        } else if (std.mem.startsWith(u8, arg, "--bench-warmups=")) {
            options.bench_warmups = try std.fmt.parseUnsigned(usize, arg["--bench-warmups=".len..], 10);
        } else if (std.mem.startsWith(u8, arg, "-")) {
            return error.Usage;
        } else if (!has_path) {
            options.path = arg;
            has_path = true;
        } else {
            return error.Usage;
        }
    }

    if (!has_path or options.top == 0) return error.Usage;
    return options;
}

fn countBytes(allocator: std.mem.Allocator, bytes: []const u8, top: usize, max_word: usize) !Result {
    var counts = std.StringHashMap(u64).init(allocator);
    var word: std.ArrayList(u8) = .empty;
    var total: u64 = 0;
    const normalized_max_word = normalizeMaxWord(max_word);

    try counts.ensureTotalCapacity(estimatedUniqueWords(bytes));
    try word.ensureTotalCapacity(allocator, @min(normalized_max_word, default_max_word));

    for (bytes) |byte| {
        if (std.ascii.isAlphabetic(byte)) {
            if (word.items.len < normalized_max_word) {
                try word.append(allocator, std.ascii.toLower(byte));
            }
        } else if (word.items.len > 0) {
            try commitWord(allocator, &counts, word.items, &total);
            word.clearRetainingCapacity();
        }
    }

    if (word.items.len > 0) {
        try commitWord(allocator, &counts, word.items, &total);
    }

    var entries: std.ArrayList(Entry) = .empty;
    try entries.ensureTotalCapacity(allocator, counts.count());
    var iterator = counts.iterator();
    while (iterator.next()) |kv| {
        try entries.append(allocator, .{ .word = kv.key_ptr.*, .count = kv.value_ptr.* });
    }

    std.mem.sort(Entry, entries.items, {}, compareEntries);
    if (entries.items.len > top) {
        entries.shrinkRetainingCapacity(top);
    }

    return .{ .total = total, .unique = counts.count(), .top = entries.items };
}

fn estimatedUniqueWords(bytes: []const u8) u32 {
    const estimate = bytes.len / estimated_bytes_per_unique_word;
    return @intCast(@min(estimate, std.math.maxInt(u32)));
}

fn normalizeMaxWord(value: usize) usize {
    if (value == 0) return default_max_word;
    if (value < min_word) return min_word;
    if (value > max_word_limit) return max_word_limit;
    return value;
}

fn commitWord(
    allocator: std.mem.Allocator,
    counts: *std.StringHashMap(u64),
    word: []const u8,
    total: *u64,
) !void {
    if (counts.getPtr(word)) |count| {
        count.* += 1;
    } else {
        const key = try allocator.dupe(u8, word);
        try counts.put(key, 1);
    }
    total.* += 1;
}

fn compareEntries(_: void, left: Entry, right: Entry) bool {
    if (left.count != right.count) return left.count > right.count;
    return std.mem.lessThan(u8, left.word, right.word);
}

fn countChecksum(
    allocator: std.mem.Allocator,
    bytes: []const u8,
    top: usize,
    max_word: usize,
) !u64 {
    var arena = std.heap.ArenaAllocator.init(allocator);
    defer arena.deinit();

    const result = try countBytes(arena.allocator(), bytes, top, max_word);
    var value = result.total ^ @as(u64, @intCast(result.unique));
    for (result.top) |entry| {
        value ^= entry.count ^ @as(u64, @intCast(entry.word.len));
    }
    return value;
}

fn renderBench(
    writer: anytype,
    io: Io,
    allocator: std.mem.Allocator,
    bytes: []const u8,
    options: Options,
) !void {
    for (0..options.bench_warmups) |_| {
        _ = try countChecksum(allocator, bytes, options.top, options.max_word);
    }

    var checksum: u64 = 0;
    const started = Io.Clock.awake.now(io).nanoseconds;
    for (0..options.bench_runs) |_| {
        checksum ^= try countChecksum(allocator, bytes, options.top, options.max_word);
    }
    const elapsed = Io.Clock.awake.now(io).nanoseconds - started;
    const mean_ms = @as(f64, @floatFromInt(elapsed)) / 1_000_000.0 / @as(f64, @floatFromInt(options.bench_runs));

    try writer.print("{{\"mean_ms\":{d:.6},\"checksum\":{}}}\n", .{ mean_ms, checksum });
}

fn renderJson(writer: anytype, result: Result) !void {
    try writer.print("{{\"total\":{},\"unique\":{},\"top\":[", .{ result.total, result.unique });
    for (result.top, 0..) |entry, index| {
        try writer.print("{s}{{\"word\":\"{s}\",\"count\":{}}}", .{
            if (index == 0) "" else ",",
            entry.word,
            entry.count,
        });
    }
    try writer.writeAll("]}\n");
}

fn renderText(writer: anytype, result: Result) !void {
    try writer.writeAll("count word\n");
    for (result.top) |entry| {
        try writer.print("{} {s}\n", .{ entry.count, entry.word });
    }
    try writer.print("total {}\nunique {}\n", .{ result.total, result.unique });
}
