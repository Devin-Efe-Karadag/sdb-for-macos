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
    id_ = is_internal ? -1 : get_next_id();
}

void sdb::breakpoint::enable() {
    is_enabled_ = true;
    breakpoint_sites_.for_each([](auto& site) { site.enable(); });
}

void sdb::breakpoint::disable() {
    is_enabled_ = false;
    breakpoint_sites_.for_each([](auto& site) { site.disable(); });
}

void sdb::address_breakpoint::resolve() {
