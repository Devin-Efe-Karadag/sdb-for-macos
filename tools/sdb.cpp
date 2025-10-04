#include <iostream>
#include <unistd.h>
#include <string_view>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <editline/readline.h>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <libsdb/process.hpp>
#include <libsdb/error.hpp>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <libsdb/disassembler.hpp>
#include <libsdb/syscalls.hpp>
#include <libsdb/target.hpp>
#include <csignal>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <libsdb/type.hpp>
#include <unordered_set>
#include <libsdb/parse.hpp>

namespace {
	sdb::process* g_sdb_process = nullptr;

	void handle_sigint(int) {
		kill(g_sdb_process->pid(), SIGSTOP);
	}

	std::unique_ptr<sdb::target> attach(int argc, const char** argv) {
		// Passing PID
		if (argc == 3 && argv[1] == std::string_view("-p")) {
			pid_t pid = std::atoi(argv[2]);
			return sdb::target::attach(pid);
		}
		// Passing program name
		else {
			auto program_path = argv[1];
			std::vector<std::string> arguments(argv + 2, argv + argc);
			auto target = sdb::target::launch(program_path, std::nullopt, arguments);
			fmt::print("Launched process with PID {}\n", target->get_process().pid());
			return target;
		}
	}

	void print_source(
		const std::filesystem::path& path, std::uint64_t line,
		std::uint64_t n_lines_context) {
		std::ifstream file{ path.string() };

		auto start_line = line <= n_lines_context ? 1 : line - n_lines_context;
		auto end_line = line + n_lines_context + 1;

		char c{};
		auto current_line = 1u;
		while (current_line != start_line && file.get(c)) {
			if (c == '\n') {
				++current_line;
			}
		}

		auto print_line_start = [&](auto current_line) {
			auto fill_width = static_cast<int>(
				std::floor(std::log10(end_line))) + 1;
			auto arrow = current_line == line ? ">" : " ";
			fmt::print("{} {:>{}} ", arrow, current_line, fill_width);
			};

		print_line_start(current_line);
		while (current_line <= end_line && file.get(c)) {
			std::cout << c;
			if (c == '\n') {
				++current_line;
				print_line_start(current_line);
			}
		}

		std::cout << std::endl;
	}

	void print_disassembly(sdb::process& process,
		sdb::virt_addr address, std::size_t n_instructions) {
		sdb::disassembler dis(process);
		auto instructions = dis.disassemble(n_instructions, address);
		for (auto& instr : instructions) {
			fmt::print("{:#018x}: {}\n", instr.address.addr(), instr.text);
		}
	}

	std::vector<std::string> split(std::string_view str, char delimiter) {
		std::vector<std::string> out{};
		std::stringstream ss{ std::string{str} };
		std::string item;

		while (std::getline(ss, item, delimiter)) {
			out.push_back(item);
		}

		return out;
	}

	bool is_prefix(std::string_view str, std::string_view of) {
		if (str.size() > of.size()) return false;
		return std::equal(str.begin(), str.end(), of.begin());
	}

	void resume(pid_t pid) {
        if (ptrace(PT_CONTINUE, pid, reinterpret_cast<caddr_t>(1), 0) < 0)
        {
			std::cerr << "Couldn't continue\n";
			std::exit(-1);
		}
	}

	void wait_on_signal(pid_t pid) {
		int wait_status;
		int options = 0;
		if (waitpid(pid, &wait_status, options) < 0) {
			std::perror("waitpid failed");
			std::exit(-1);
		}
	}

	void handle_command(
		pid_t pid, std::string_view line) {
		auto args = split(line, ' ');
		auto command = args[0];

		if (is_prefix(command, "continue")) {
			resume(pid);
			wait_on_signal(pid);
		}
		else {
			std::cerr << "Unknown command\n";
		}
	}

	void thread_lifecycle_callback(const sdb::stop_reason& reason) {
		std::string_view action;
		switch (reason.reason) {
		case sdb::process_state::exited: action = "exited"; break;
		case sdb::process_state::terminated: action = "terminated"; break;
		case sdb::process_state::stopped: action = "created"; break;
		case sdb::process_state::running: return;
		}
		fmt::print("Thread {} {}\n", reason.tid, action);
	}

    std::string get_sigtrap_info(
        const sdb::process& process, sdb::stop_reason reason) {
        if (reason.trap_reason == sdb::trap_type::software_break) {
            auto& site = process.breakpoint_sites().get_by_address(process.get_pc(reason.tid));

            return fmt::format(" (breakpoint {})", site.id());
        }

		if (reason.trap_reason == sdb::trap_type::hardware_break) {
			auto id = process.get_current_hardware_stoppoint(reason.tid);

			if (id.index() == 0) {
				return fmt::format(" (breakpoint {})", std::get<0>(id));
			}

			std::string message;
			auto& point = process.watchpoints().get_by_id(std::get<1>(id));
			message += fmt::format(" (watchpoint {})", point.id());

			if (point.data() == point.previous_data()) {
				message += fmt::format("\nValue: {:#x}", point.data());
			}
			else {
				message += fmt::format("\nOld value: {:#x}\nNew value: {:#x}",
					point.previous_data(), point.data());
			}
			return message;
		}
		if (reason.trap_reason == sdb::trap_type::single_step) {
			return " (single step)";
		}
		if (reason.trap_reason == sdb::trap_type::syscall) {
			const auto& info = *reason.syscall_info;
			std::string message;
			if (info.entry) {
				message += "(syscall entry)\n";
				message += fmt::format("syscall: {}({:#x})",
					sdb::syscall_id_to_name(info.id),
					fmt::join(info.args, ","));
			}
			else {
				message += "(syscall exit)\n";
				message += fmt::format("syscall returned: {:#x}", info.ret);
			}
			return message;
		}

		return "";
	}

	std::string get_signal_stop_reason(
		const sdb::target& target, sdb::stop_reason reason) {
		auto& process = target.get_process();
		auto pc = process.get_pc(reason.tid);
		std::string message = fmt::format("stopped with signal {} at {:#x}",
			strsignal(reason.info), pc.addr());

		auto line = target.line_entry_at_pc(reason.tid);
		if (line != sdb::line_table::iterator()) {
			auto file = line->file_entry->path.filename().string();
			message += fmt::format(", {}:{}", file, line->line);
		}

		auto func_name = target.function_name_at_address(pc);
		if (func_name != "") {
			message += fmt::format(" ({})", func_name);
		}

		if (reason.info == SIGTRAP) {
			message += get_sigtrap_info(process, reason);
		}

		return message;
	}

	void print_stop_reason(
		const sdb::target& target, sdb::stop_reason reason) {
		switch (reason.reason) {
		case sdb::process_state::running:
			return;
		case sdb::process_state::exited:
			fmt::print("Process {} exited with status {}\n",
				target.get_process().pid(),
				static_cast<int>(reason.info));
			return;;
		case sdb::process_state::terminated:
			fmt::print("Process {} terminated with signal {}\n",
				target.get_process().pid(),
				strsignal(reason.info));
			return;
		case sdb::process_state::stopped:
			fmt::print("Thread {} {}\n",
				reason.tid, get_signal_stop_reason(target, reason));
			return;
		}
	}

	void print_code_location(sdb::target& target) {
		if (target.get_stack().has_frames()) {
			auto& frame = target.get_stack().current_frame();
			print_source(frame.location.file->path, frame.location.line, 3);
		}
		else {
			print_disassembly(target.get_process(), target.get_process().get_pc(), 5);
		}
	}

	void handle_stop(sdb::target& target, sdb::stop_reason reason) {
		print_stop_reason(target, reason);
		if (reason.reason == sdb::process_state::stopped) {
			print_code_location(target);
		}
	}

	void print_help(const std::vector<std::string>& args) {
		if (args.size() == 1) {
			std::cerr << R"(Available commands:
    breakpoint  - Commands for operating on breakpoints
    catchpoint  - Commands for operating on catchpoints

    continue    - Resume the process
    disassemble - Disassemble machine code to assembly
    down        - Select the stack frame below the current one
    finish      - Step-out
    memory      - Commands for operating on memory
    next        - Step-over
    register    - Commands for operating on registers
    step        - Step-in
    stepi       - Single instruction step
	thread      - Commands for operating on threads
    up          - Select the stack frame above the current one
    variable    - Commands for operating on variables
    watchpoint  - Commands for operating on watchpoints
)";
		}
		else if (is_prefix(args[1], "memory")) {
			std::cerr << R"(Available commands:
    read <address>
    read <address> <number of bytes>
    write <address> <bytes>
)";
		}
		else if (is_prefix(args[1], "breakpoint")) {
			std::cerr << R"(Available commands:
    list
    delete <id>
    disable <id>
    enable <id>
    set <address>
    set <address> -h
)";
		}

		else if (is_prefix(args[1], "register")) {
			std::cerr << R"(Available commands:
    read
    read <register>
    read all
    write <register> <value>
)";
		}
		else if (is_prefix(args[1], "watchpoint")) {
			std::cerr << R"(Available commands:
    list
    delete <id>
    disable <id>
    enable <id>
    set <address> <write|rw|execute> <size>
)";
		}
		else if (is_prefix(args[1], "disassemble")) {
			std::cerr << R"(Available options:
    -c <number of instructions>
    -a <start address>
)";
		}
		else if (is_prefix(args[1], "catchpoint")) {
			std::cerr << R"(Available commands:
    syscall
    syscall none
