#ifndef SDB_BREAKPOINT_HPP
#define SDB_BREAKPOINT_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <libsdb/stoppoint_collection.hpp>
#include <libsdb/breakpoint_site.hpp>
#include <libsdb/types.hpp>
#include <filesystem>
#include <functional>

namespace sdb {
	class target;

	class breakpoint {
	public:
		virtual ~breakpoint() = default;

		breakpoint() = delete;
		breakpoint(const breakpoint&) = delete;
		breakpoint& operator=(const breakpoint&) = delete;

		using id_type = std::int32_t;
		id_type id() const { return id_; }

		void enable();
		void disable();

		bool is_enabled() const { return is_enabled_; }
		bool is_hardware() const { return is_hardware_; }
		bool is_internal() const { return is_internal_; }

		virtual void resolve() = 0;

		stoppoint_collection<breakpoint_site, false>&
			breakpoint_sites() { return breakpoint_sites_; }
		const stoppoint_collection<breakpoint_site, false>&
			breakpoint_sites() const { return breakpoint_sites_; }

		bool at_address(virt_addr addr) const {
			return breakpoint_sites_.contains_address(addr);
		}
		bool in_range(virt_addr low, virt_addr high) const {
			return !breakpoint_sites_.get_in_region(low, high).empty();
		}

		void install_hit_handler(std::function<bool(void)> on_hit) {
			on_hit_ = std::move(on_hit);
		}

		bool notify_hit() const {
			if (on_hit_) return on_hit_();
			return false;
		}

	protected:
		friend target;
		breakpoint(
			target& tgt, bool is_hardware = false, bool is_internal = false);
