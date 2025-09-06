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
			none, some, all
		};

		static syscall_catch_policy catch_all() {
			return { mode::all, {} };
		}

		static syscall_catch_policy catch_none() {
			return { mode::none, {} };
		}

		static syscall_catch_policy catch_some(std::vector<int> to_catch) {
			return { mode::some, std::move(to_catch) };
		}

		mode get_mode() const { return mode_; }
		const std::vector<int>& get_to_catch() const { return to_catch_; }

	private:
		syscall_catch_policy(mode mode, std::vector<int> to_catch) :
			mode_(mode), to_catch_(std::move(to_catch)) {}

		mode mode_ = mode::none;
		std::vector<int> to_catch_;
	};

	struct syscall_information {
		std::uint16_t id;
		bool entry;
		union {
			std::array<std::uint64_t, 6> args;
			std::int64_t ret;
		};
