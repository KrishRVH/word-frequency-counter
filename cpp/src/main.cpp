#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace
{

constexpr auto default_max_word = std::size_t{ 64 };
constexpr auto max_word_limit = std::size_t{ 1024 };
constexpr auto min_word = std::size_t{ 4 };

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
    const auto lower = static_cast<unsigned char>(byte | 32U);
    return lower >= static_cast<unsigned char>('a') &&
           lower <= static_cast<unsigned char>('z');
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
    auto parse_option = [&](std::string_view arg,
                            std::string_view name,
                            std::string_view prefix,
                            std::size_t &target,
                            int &index) -> bool {
        if (arg == name) {
            if (++index >= argc) {
                throw std::invalid_argument{
                    "usage: wordcount_cpp [--json] [--top N] [--max-word N] "
                    "<file>"
                };
            }
            target = parse_size(argv[index]);
            return true;
        }

        if (arg.starts_with(prefix)) {
            target = parse_size(arg.substr(prefix.size()));
            return true;
        }

        return false;
    };

    for (int index = 1; index < argc; ++index) {
        const std::string_view arg{ argv[index] };

        if (arg == "--json") {
            options.json = true;
        } else if (parse_option(arg, "--top", "--top=", options.top, index) ||
                   parse_option(arg,
                                "--max-word",
                                "--max-word=",
                                options.max_word,
                                index)) {
            continue;
        } else if (options.path.empty() && !arg.starts_with("-")) {
            options.path = std::string{ arg };
        } else {
            throw std::invalid_argument{
                "usage: wordcount_cpp [--json] [--top N] [--max-word N] <file>"
            };
        }
    }

    if (options.path.empty() || options.top == 0) {
        throw std::invalid_argument{
            "usage: wordcount_cpp [--json] [--top N] [--max-word N] <file>"
        };
    }
    options.max_word = normalize_max_word(options.max_word);

    return options;
}

[[nodiscard]] auto read_file(const std::string &path)
        -> std::vector<unsigned char>
{
    std::ifstream file{ path, std::ios::binary };
    if (!file) {
        throw std::runtime_error{ "cannot open input file" };
    }

    auto bytes =
            std::vector<unsigned char>{ std::istreambuf_iterator<char>{ file },
                                        std::istreambuf_iterator<char>{} };
    if (file.bad()) {
        throw std::runtime_error{ "cannot read input file" };
    }
    return bytes;
}

[[nodiscard]] auto count_words(const std::vector<unsigned char> &bytes,
                               std::size_t top,
                               std::size_t max_word) -> Result
{
    std::unordered_map<std::string, std::uint64_t> counts;
    std::string word;
    std::uint64_t total = 0;
    bool in_word = false;

    max_word = normalize_max_word(max_word);
    word.reserve(std::min<std::size_t>(max_word, 64));

    for (const auto byte : bytes) {
        if (is_letter(byte)) {
            in_word = true;
            if (word.size() < max_word) {
                word.push_back(lower_ascii(byte));
            }
            continue;
        }

        if (in_word) {
            ++counts[word];
            ++total;
            word.clear();
            in_word = false;
        }
    }

    if (in_word) {
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
    std::cout << "{\"total\":" << result.total
              << ",\"unique\":" << result.unique << ",\"top\":[";
    for (std::size_t index = 0; index < result.top.size(); ++index) {
        const auto &entry = result.top[index];
        std::cout << (index == 0 ? "" : ",") << "{\"word\":\"" << entry.word
                  << "\",\"count\":" << entry.count << "}";
    }
    std::cout << "]}\n";
}

void render_text(const Result &result)
{
    std::cout << "count word\n";
    for (const auto &entry : result.top) {
        std::cout << entry.count << ' ' << entry.word << '\n';
    }
    std::cout << "total " << result.total << "\nunique " << result.unique
              << '\n';
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
        std::cerr << "wordcount_cpp: " << error.what() << '\n';
        return 1;
    }
}
