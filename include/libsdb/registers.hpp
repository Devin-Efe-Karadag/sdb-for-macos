#ifndef SDB_REGISTERS_HPP
#define SDB_REGISTERS_HPP

#include <libsdb/detail/native_user.hpp>
#include <libsdb/register_info.hpp>
#include <variant>
#include <libsdb/types.hpp>

namespace sdb {
    class process;

    class registers {
    public:
        registers() = default;
        registers(const registers&) = default;
        registers& operator=(const registers&) = default;

        using value = std::variant<
            std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t,

            std::int8_t, std::int16_t, std::int32_t, std::int64_t,
            float, double, long double,
            byte64, byte128>;
