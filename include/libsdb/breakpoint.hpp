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
