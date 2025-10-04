#include <libsdb/breakpoint_site.hpp>
#include <libsdb/error.hpp>
	auto get_next_id() {
		return ++id;
}
	process& proc, virt_addr address, bool is_hardware, bool is_internal)
	is_internal_{ is_internal } {
sdb::breakpoint_site::breakpoint_site(
	bool is_hardware, bool is_internal)
	is_enabled_{ false }, saved_data_{},
    if (is_enabled_) return;

    if (is_hardware_) hardware_register_index_ = process_->set_hardware_breakpoint(id_, address_);
        std::uint32_t trap = 0xd4200000;
