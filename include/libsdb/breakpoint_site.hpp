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
