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

        return ret;
    }

    template<>
    inline std::optional<std::byte> to_integral(std::string_view sv, int base) {
        auto uint8 = to_integral<std::uint8_t>(sv, base);

        if (uint8) return static_cast<std::byte>(*uint8);

        return std::nullopt;
    }

    inline std::vector<std::byte> parse_vector(std::string_view text) {
        if(text.size()<2 || text.front()!='[' || text.back()!=']') error::send("Expected [0x00,0x01,...]");
        text.remove_prefix(1);text.remove_suffix(1);std::vector<std::byte> result;

        while(!text.empty()){
            auto comma=text.find(',');auto item=text.substr(0,comma);auto value=to_integral<std::byte>(item,16);

            if(!value)error::send("Invalid byte value");result.push_back(*value);

            if(comma==std::string_view::npos)break;
            text.remove_prefix(comma+1);if(text.empty())error::send("Trailing comma in byte vector");
        }return result;
    }
    template <std::size_t N> auto parse_vector(std::string_view text) {
        auto parsed=parse_vector(text);if(parsed.size()!=N)error::send("Incorrect register vector size");

        std::array<std::byte,N> bytes{};std::copy(parsed.begin(),parsed.end(),bytes.begin());return bytes;
    }

    template <class F>
