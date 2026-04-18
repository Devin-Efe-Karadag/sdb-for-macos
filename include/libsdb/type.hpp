#ifndef SDB_TYPE_HPP
#define SDB_TYPE_HPP

#include <string_view>
#include <optional>
#include <libsdb/dwarf.hpp>
#include <vector>
#include <variant>

namespace sdb {
    enum class parameter_class {
        integer, sse, sseup, x87, x87up, complex_x87,
        memory, no_class
    };

    enum class builtin_type {
        string, character, integer, boolean, floating_point
    };

    class process;

    class type {
    public:
        type(die die)
            : info_(std::move(die)) {}

        std::size_t byte_size() const;

        bool is_char_type() const;

        template<int... Tags>
        type strip() const {
            auto ret = *this;

            auto tag = ret.get_die().abbrev_entry()->tag;

            while (((tag == Tags) or ...)) {
                ret = ret.get_die()[DW_AT_type].as_type().get_die();
                tag = ret.get_die().abbrev_entry()->tag;
            }

            return ret;
        }

        type strip_cv_typedef() const {
            return strip<DW_TAG_const_type,
                DW_TAG_volatile_type,
                DW_TAG_typedef>();
        }
        type strip_cvref_typedef() const {
            return strip<DW_TAG_const_type,
                DW_TAG_volatile_type,
                DW_TAG_typedef,
                DW_TAG_reference_type,
                DW_TAG_rvalue_reference_type>();
        }
        type strip_all() const {
            return strip<DW_TAG_const_type,
                DW_TAG_volatile_type,
                DW_TAG_typedef,
                DW_TAG_reference_type,
                DW_TAG_rvalue_reference_type, 
                DW_TAG_pointer_type>();
        }

        type(builtin_type type)
            : info_(type) {}

        die get_die() const {
            if (!std::holds_alternative<die>(info_)) {
                sdb::error::send("Type is not from DWARF info");
            }

            return std::get<die>(info_);
        }
