#include <libsdb/breakpoint.hpp>
#include <libsdb/target.hpp>

namespace {
    auto get_next_id() {
        static sdb::breakpoint::id_type id = 0;

        return ++id;
    }
}

sdb::breakpoint::breakpoint(
    target& tgt, bool is_hardware, bool is_internal)
    : target_{ &tgt }, is_hardware_{ is_hardware },
    is_internal_{ is_internal } {
