#include <libsdb/breakpoint_site.hpp>
#include <sys/ptrace.h>
#include <libsdb/breakpoint_site.hpp>
#include <libsdb/process.hpp>
#include <libsdb/error.hpp>

namespace {
	auto get_next_id() {
		static sdb::breakpoint_site::id_type id = 0;
		return ++id;
	}
}

sdb::breakpoint_site::breakpoint_site(
	process& proc, virt_addr address, bool is_hardware, bool is_internal)
	: process_{ &proc }, address_{ address }, is_enabled_{ false },
	saved_data_{}, is_hardware_{ is_hardware },
	is_internal_{ is_internal } {
	id_ = is_internal_ ? -1 : get_next_id();
}

sdb::breakpoint_site::breakpoint_site(
	breakpoint* parent, id_type id,
	process& proc, virt_addr address,
	bool is_hardware, bool is_internal)
	: parent_{ parent }, id_(id),
	process_{ &proc }, address_{ address },
	is_enabled_{ false }, saved_data_{},
	is_hardware_{ is_hardware }, is_internal_{ is_internal } {
}
    if (is_enabled_) return;

    if (is_hardware_) hardware_register_index_ = process_->set_hardware_breakpoint(id_, address_);
    else {
        if (address_.addr() % 4) error::send("ARM64 breakpoints require a 4-byte aligned address");
        std::uint32_t trap = 0xd4200000;
        process_->write_memory(address_, to_byte_span(trap));
    }
}
void sdb::breakpoint_site::disable() {
    if (is_hardware_) process_->clear_hardware_stoppoint(hardware_register_index_);
    else process_->write_memory(address_, to_byte_span(saved_data_));
}
