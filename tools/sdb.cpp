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
