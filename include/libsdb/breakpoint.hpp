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
