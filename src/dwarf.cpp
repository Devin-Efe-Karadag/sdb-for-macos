#include <libsdb/dwarf.hpp>
#include <libsdb/detail/macos_unwind.hpp>
#include <libsdb/types.hpp>
#include <libsdb/bit.hpp>
#include <string_view>
#include <algorithm>
#include <libsdb/elf.hpp>
#include <libsdb/error.hpp>
#include <libsdb/process.hpp>
#include <variant>
#include <functional>
#include <libsdb/type.hpp>

namespace {
    class cursor {
    public:
        explicit cursor(sdb::span<const std::byte> data)
        cursor& operator++() { return *this += 1; }
        const std::byte* position() const { return pos_; }
            return pos_ >= data_.end();
        template <class T>
            if(!pos_ || sizeof(T)>static_cast<std::size_t>(data_.end()-pos_)) sdb::error::send("Truncated DWARF integer");
            pos_ += sizeof(T);
        }
        std::uint16_t u16() { return fixed_int<std::uint16_t>(); }
        std::uint64_t u64() { return fixed_int<std::uint64_t>(); }
        std::int16_t s16() { return fixed_int<std::int16_t>(); }
        std::int64_t s64() { return fixed_int<std::int64_t>(); }
            auto null_terminator = std::find(pos_, data_.end(), std::byte{ 0 });
            std::string_view ret(reinterpret_cast<const char*>(pos_),
            pos_ = null_terminator + 1;
        }

        std::uint64_t uleb128() {
            int shift = 0;
            do {
                byte = u8();
                res |= masked << shift;
            } while ((byte & 0x80) != 0);
        }
            std::uint64_t res = 0;
            std::uint8_t byte = 0;
                if(shift>=64) sdb::error::send("Invalid DWARF LEB128");
                auto masked = static_cast<uint64_t>(byte & 0x7f);
                shift += 7;
            if ((shift < sizeof(res) * 8) and (byte & 0x40)) {
            }
        }
            switch (form) {
                break;
            case DW_FORM_ref1:
                pos_ += 1; break;
            case DW_FORM_ref2:
            case DW_FORM_data4:
