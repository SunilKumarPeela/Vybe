#pragma once
#include "point_core.h"
#include <algorithm>
#include <utility>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace point::file_diff {
struct FieldPair { std::size_t left; std::size_t right; };
struct Options {
    bool trim_whitespace = true;
    bool ignore_ascii_case = false;
    bool count_duplicates = true;
    bool match_empty_keys = false;
};
struct Row { bool left; std::size_t index; bool empty_key; };
struct Result {
    std::vector<Row> rows;
    std::size_t left_only = 0, right_only = 0;
    std::size_t matched_left = 0, matched_right = 0;
};
inline std::string cell(const DataSet& data, std::size_t row, std::size_t col) {
    return col < data.rows.at(row).size() ? data.rows[row][col] : std::string{};
}
// Length-prefix each component: delimiters embedded in cells cannot collide.
// Preserve punctuation, leading zeros and internal spaces; never infer identity.
inline std::pair<std::string, bool> key(const DataSet& data, std::size_t row,
        const std::vector<FieldPair>& fields, bool left, const Options& options) {
    std::string output;
    bool empty = true;
    for (const auto& pair : fields) {
        auto value = cell(data, row, left ? pair.left : pair.right);
        if (options.trim_whitespace) value = trim(value);
        empty = empty && value.empty();
        if (options.ignore_ascii_case) {
            for (auto& ch : value)
                if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        }
        output += std::to_string(value.size()) + ':' + value;
    }
    return {std::move(output), empty};
}
inline Result compare(const DataSet& a, const DataSet& b,
        const std::vector<FieldPair>& fields, const Options& options = {},
        const std::function<bool()>& cancelled = {}) {
    if (fields.empty()) throw std::invalid_argument("Add at least one field pair.");
    std::unordered_set<std::size_t> left_fields, right_fields;
    for (const auto& pair : fields) {
        if (pair.left >= a.headers.size() || pair.right >= b.headers.size())
            throw std::invalid_argument("Selected field no longer exists.");
        if (!left_fields.insert(pair.left).second || !right_fields.insert(pair.right).second)
            throw std::invalid_argument("Each field can appear in only one pair.");
    }
    auto check = [&]() {
        if (cancelled && cancelled()) throw std::runtime_error("Comparison cancelled.");
    };
    // Map to a chain of source row indices. No joined rows or Cartesian products.
    const auto none = static_cast<std::size_t>(-1);
    std::unordered_map<std::string, std::size_t> heads;
    std::vector<std::size_t> next(b.rows.size(), none);
    std::vector<bool> matched(b.rows.size(), false);
    std::vector<bool> empty_right(b.rows.size(), false);
    for (std::size_t end = b.rows.size(); end > 0; --end) {
        check();
        const auto i = end - 1;
        auto [value, empty] = key(b, i, fields, false, options);
        empty_right[i] = empty;
        if (empty && !options.match_empty_keys) continue;
        auto [it, inserted] = heads.try_emplace(std::move(value), i);
        if (!inserted) { next[i] = it->second; it->second = i; }
    }
    Result result;
    for (std::size_t i = 0; i < a.rows.size(); ++i) {
        check();
        auto [value, empty] = key(a, i, fields, true, options);
        auto found = heads.find(value);
        if ((empty && !options.match_empty_keys) || found == heads.end() || found->second == none) {
            result.rows.push_back({true, i, empty});
            ++result.left_only;
            continue;
        }
        ++result.matched_left;
        const auto index = found->second;
        if (options.count_duplicates) {
            matched[index] = true;
            ++result.matched_right;
            found->second = next[index];
        } else if (!matched[index]) {
            for (auto j = index; j != none; j = next[j]) {
                check();
                matched[j] = true;
                ++result.matched_right;
            }
        }
    }
    for (std::size_t i = 0; i < b.rows.size(); ++i) {
        check();
        if (!matched[i]) {
            result.rows.push_back({false, i, empty_right[i]});
            ++result.right_only;
        }
    }
    return result;
}
} // namespace point::file_diff
