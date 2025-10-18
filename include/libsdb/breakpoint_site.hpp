#ifndef SDB_BREAKPOINT_SITE_HPP
#define SDB_BREAKPOINT_SITE_HPP

#include <cstdint>
#include <cstddef>
#include <libsdb/types.hpp>

namespace sdb {
	class process;

	class breakpoint;
	class breakpoint_site {
	public:
		breakpoint_site() = delete;
		breakpoint_site(const breakpoint_site&) = delete;
		breakpoint_site& operator=(const breakpoint_site&) = delete;

		using id_type = std::int32_t;
		id_type id() const { return id_; }

		void enable();
		void disable();

		bool is_enabled() const { return is_enabled_; }
		virt_addr address() const { return address_; }

		bool at_address(virt_addr addr) const {
			return address_ == addr;
		}
