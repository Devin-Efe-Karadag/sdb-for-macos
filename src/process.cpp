#include <libsdb/process.hpp>
#include <libsdb/target.hpp>
#include <libsdb/pipe.hpp>
#include <mach/mach_vm.h>
#include <mach-o/loader.h>
#include <libproc.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>
#include <limits>
}
std::unique_ptr<sdb::process> sdb::process::launch(std::filesystem::path path, bool debug, std::optional<int> output, const std::vector<std::string>& arguments) {
    pipe channel(true);

    std::vector<char*> exec_arguments{const_cast<char*>(path.c_str())};

    for (auto& argument : arguments) exec_arguments.push_back(const_cast<char*>(argument.c_str()));
    exec_arguments.push_back(nullptr);

    auto pid = fork();

    if (pid < 0) error::send_errno("fork");

    if (!pid) {
        channel.close_read();

        if (output && dup2(*output, STDOUT_FILENO) < 0) _exit(126);

        if (debug && ptrace(PT_TRACE_ME, 0, nullptr, 0) < 0) _exit(126);
        execvp(path.c_str(), exec_arguments.data());

        int code = errno;
        ::write(channel.get_write(), &code, sizeof(code));
        _exit(127);
    }
    channel.close_write();

    auto failure = channel.read();

    if (!failure.empty()) { wait_child(pid); error::send("exec failed: " + std::string(strerror(from_bytes<int>(failure.data())))); }

    int status = debug ? wait_child(pid) : 0;

    if (debug && !WIFSTOPPED(status)) error::send("Debuggee exited before its initial stop");
    try {
        auto proc = std::unique_ptr<process>(new process(pid, true, debug));

        return proc;
