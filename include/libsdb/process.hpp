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
	};

	enum class process_state {
		stopped,
		running,
		exited,
		terminated
	};

	enum class trap_type {
		single_step, software_break,
		hardware_break, syscall, clone, unknown
	};

	struct stop_reason {
		stop_reason() = default;
		stop_reason(pid_t tid, int wait_status);

		stop_reason(pid_t tid, process_state reason, std::uint8_t info,
			std::optional<trap_type> trap_reason = std::nullopt,
			std::optional<syscall_information> syscall_info = std::nullopt)
			: reason(reason)
			, info(info)
			, trap_reason(trap_reason)
			, syscall_info(syscall_info)
			, tid(tid)
		{}

		bool is_step() const {
			return reason == process_state::stopped
				and info == SIGTRAP
				and trap_reason == trap_type::single_step;
		}
		bool is_breakpoint() const {
			return reason == process_state::stopped
				and info == SIGTRAP
				and (trap_reason == trap_type::software_break
					or trap_reason == trap_type::hardware_break);
		}

		process_state reason;
		std::uint8_t info;
		std::optional<trap_type> trap_reason;
		std::optional<syscall_information> syscall_info;
		pid_t tid;
	};

	struct thread_state {
		pid_t tid;
		registers regs;
		stop_reason reason;
		process_state state = process_state::stopped;
		bool pending_sigstop = false;
	};

	class target;
	class process {
	public:
		~process();
		static std::unique_ptr<process> launch(std::filesystem::path path,
			bool debug = true,
			std::optional<int> stdout_replacement = std::nullopt,
			const std::vector<std::string>& arguments = {});
		static std::unique_ptr<process> attach(pid_t pid);

        mach_port_t task_port() const { return task_; }
