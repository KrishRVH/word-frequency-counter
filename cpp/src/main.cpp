#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace
{

constexpr auto default_max_word = std::size_t{ 64 };
constexpr auto estimated_bytes_per_unique_word = std::size_t{ 32 };
constexpr auto max_word_limit = std::size_t{ 1024 };
constexpr auto min_word = std::size_t{ 4 };
constexpr auto usage =
        "usage: wordcount_cpp [--json] [--top N] [--max-word N] <file>";

struct Entry {
    std::string word;
    std::uint64_t count;
};

struct Result {
    std::uint64_t total;
    std::size_t unique;
    std::vector<Entry> top;
};

struct Options {
    std::string path;
    std::size_t top = 10;
    std::size_t max_word = 1024;
    bool json = false;
};

[[nodiscard]] auto is_letter(unsigned char byte) -> bool
{
    return (byte >= static_cast<unsigned char>('A') &&
            byte <= static_cast<unsigned char>('Z')) ||
           (byte >= static_cast<unsigned char>('a') &&
            byte <= static_cast<unsigned char>('z'));
}

[[nodiscard]] auto lower_ascii(unsigned char byte) -> char
{
    if (byte >= static_cast<unsigned char>('A') &&
        byte <= static_cast<unsigned char>('Z')) {
        return static_cast<char>(byte + 32U);
    }
    return static_cast<char>(byte);
}

[[nodiscard]] auto parse_size(std::string_view text) -> std::size_t
{
    if (text.empty()) {
        throw std::invalid_argument{ "expected numeric option value" };
    }

    auto parsed = std::size_t{};
    const auto *begin = text.data();
    const auto *end = begin + text.size();
    const auto [cursor, error] = std::from_chars(begin, end, parsed);

    if (error != std::errc{} || cursor != end) {
        throw std::invalid_argument{ "expected numeric option value" };
    }

    return parsed;
}

[[nodiscard]] auto normalize_max_word(std::size_t value) -> std::size_t
{
    if (value == 0) {
        return default_max_word;
    }
    return std::clamp(value, min_word, max_word_limit);
}

[[nodiscard]] auto parse_args(int argc, char **argv) -> Options
{
    Options options;

    for (int index = 1; index < argc; ++index) {
        const std::string_view arg{ argv[index] };

        if (arg == "--json") {
            options.json = true;
        } else if (arg == "--top" || arg == "--max-word") {
            if (++index >= argc) {
                throw std::invalid_argument{ usage };
            }
            (arg == "--top" ? options.top : options.max_word) =
                    parse_size(argv[index]);
        } else if (arg.starts_with("--top=")) {
            options.top = parse_size(arg.substr(6));
        } else if (arg.starts_with("--max-word=")) {
            options.max_word = parse_size(arg.substr(11));
        } else if (options.path.empty() && !arg.starts_with("-")) {
            options.path = std::string{ arg };
        } else {
            throw std::invalid_argument{ usage };
        }
    }

    if (options.path.empty() || options.top == 0) {
        throw std::invalid_argument{ usage };
    }
    options.max_word = normalize_max_word(options.max_word);

    return options;
}

[[nodiscard]] auto read_file(const std::string &path)
        -> std::vector<unsigned char>
{
    std::ifstream file{ path, std::ios::binary | std::ios::ate };
    if (!file) {
        throw std::runtime_error{ "cannot open input file" };
    }

    const auto size = file.tellg();
    if (size < 0) {
        throw std::runtime_error{ "cannot read input file" };
    }

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            throw std::runtime_error{ "cannot read input file" };
        }
    }

    return bytes;
}

[[nodiscard]] auto
estimated_unique_words(const std::vector<unsigned char> &bytes) -> std::size_t
{
    return bytes.size() / estimated_bytes_per_unique_word;
}

[[nodiscard]] auto count_words(const std::vector<unsigned char> &bytes,
                               std::size_t top,
                               std::size_t max_word) -> Result
{
    std::unordered_map<std::string, std::uint64_t> counts;
    std::string word;
    std::uint64_t total = 0;

    max_word = normalize_max_word(max_word);
    counts.reserve(estimated_unique_words(bytes));
    word.reserve(std::min(max_word, default_max_word));

    for (const auto byte : bytes) {
        if (is_letter(byte)) {
            if (word.size() < max_word) {
                word.push_back(lower_ascii(byte));
            }
            continue;
        }

        if (!word.empty()) {
            ++counts[word];
            ++total;
            word.clear();
        }
    }

    if (!word.empty()) {
        ++counts[word];
        ++total;
    }

    std::vector<Entry> entries;
    entries.reserve(counts.size());
    for (auto &[entry_word, count] : counts) {
        entries.push_back({ entry_word, count });
    }

    std::ranges::sort(entries, [](const Entry &left, const Entry &right) {
        if (left.count != right.count) {
            return left.count > right.count;
        }
        return left.word < right.word;
    });

    if (entries.size() > top) {
        entries.resize(top);
    }

    return { .total = total,
             .unique = counts.size(),
             .top = std::move(entries) };
}

void render_json(const Result &result)
{
    std::print("{{\"total\":{},\"unique\":{},\"top\":[",
               result.total,
               result.unique);
    for (std::size_t index = 0; index < result.top.size(); ++index) {
        const auto &entry = result.top[index];
        std::print("{}{{\"word\":\"{}\",\"count\":{}}}",
                   index == 0 ? "" : ",",
                   entry.word,
                   entry.count);
    }
    std::println("]}}");
}

void render_text(const Result &result)
{
    std::println("count word");
    for (const auto &entry : result.top) {
        std::println("{} {}", entry.count, entry.word);
    }
    std::println("total {}\nunique {}", result.total, result.unique);
}

}  // namespace

auto main(int argc, char **argv) -> int
{
    try {
        const auto options = parse_args(argc, argv);
        const auto result = count_words(
                read_file(options.path), options.top, options.max_word);
        options.json ? render_json(result) : render_text(result);
        return 0;
    } catch (const std::exception &error) {
        (void)std::fprintf(stderr, "wordcount_cpp: %s\n", error.what());
        return 1;
    }
}
