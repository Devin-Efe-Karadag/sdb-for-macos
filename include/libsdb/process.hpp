#ifndef SDB_PROCESS_HPP
#define SDB_PROCESS_HPP

#include <filesystem>
#include <mach/mach.h>
#include <memory>
#include <optional>
#include <sys/types.h>
#include <libsdb/registers.hpp>
#include <vector>
#include <libsdb/breakpoint_site.hpp>
#include <libsdb/stoppoint_collection.hpp>
#include <libsdb/bit.hpp>
#include <libsdb/watchpoint.hpp>
#include <unordered_map>
#include <csignal>
#include <functional>

namespace sdb {

	class syscall_catch_policy {
	public:
		enum mode {
