#include <libsdb/registers.hpp>
#include <libsdb/bit.hpp>
#include <iostream>
#include <libsdb/process.hpp>
#include <type_traits>
#include <algorithm>
namespace {
    template <class T>
    sdb::byte128 widen(const sdb::register_info& info, T t) {
        using namespace sdb;

        if constexpr (std::is_floating_point_v<T>) {
            if (info.format == register_format::double_float)
                return to_byte128(static_cast<double>(t));
            if (info.format == register_format::long_double)
                return to_byte128(static_cast<long double>(t));
        }
        else if constexpr (std::is_signed_v<T>) {
            if (info.format == register_format::uint) {
                switch (info.size) {
                case 2: return to_byte128(static_cast<std::int16_t>(t));
                case 4: return to_byte128(static_cast<std::int32_t>(t));
                case 8: return to_byte128(static_cast<std::int64_t>(t));
                }
            }
        }

        return to_byte128(t);
}
    if (is_undefined(info.id))
    auto bytes = as_bytes(data_);
        switch (info.size) {
        case 2: return from_bytes<std::uint16_t>(bytes + info.offset);
        case 8: return from_bytes<std::uint64_t>(bytes + info.offset);
        }
    else if (info.format == register_format::double_float) {
    }
        return from_bytes<long double>(bytes + info.offset);
    else if (info.format == register_format::vector and info.size == 8) {
    }
