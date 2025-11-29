#ifndef SDB_PARSE_HPP
#define SDB_PARSE_HPP

#include <charconv>
#include <vector>
#include <algorithm>
#include <libsdb/error.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <array>

namespace sdb {
    template <class I>

    std::optional<I> to_integral(std::string_view sv, int base = 10) {
        auto begin = sv.begin();

        if (base == 16 and sv.size() > 1 and
            begin[0] == '0' and begin[1] == 'x') {
            begin += 2;
        }

        I ret;

        auto result = std::from_chars(begin, sv.end(), ret, base);

        if (result.ec != std::errc{} || result.ptr != sv.end()) {
            return std::nullopt;
        }
