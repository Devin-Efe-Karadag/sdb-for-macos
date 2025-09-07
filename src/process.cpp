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

namespace {
void check(kern_return_t k, const char* what) {
    if (k != KERN_SUCCESS) sdb::error::send(std::string(what) + ": " + mach_error_string(k));
}
int wait_child(pid_t pid) {
    int status;

    while (waitpid(pid, &status, 0) < 0) if (errno != EINTR) sdb::error::send_errno("waitpid");

    return status;
}
void trace(int request, pid_t pid, int signal = 0) {
    if (ptrace(request, pid, reinterpret_cast<caddr_t>(1), signal) < 0) sdb::error::send_errno("ptrace");
}
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
    } catch (...) {
        ptrace(PT_KILL, pid, nullptr, 0); ptrace(PT_CONTINUE, pid, reinterpret_cast<caddr_t>(1), 0);
        wait_child(pid); throw;
    }
}
std::unique_ptr<sdb::process> sdb::process::attach(pid_t pid) {
    if (pid <= 0 || pid == getpid()) error::send("Invalid process ID");
    // PT_ATTACHEXC delivers signals through a Mach exception port, but sdb's
    // tracing loop consumes signal stops with waitpid. Keep PT_ATTACH until the
    // debugger has a Mach exception server rather than silently changing its
    // event-delivery model.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    trace(PT_ATTACH, pid);
#pragma clang diagnostic pop
    wait_child(pid);
    try { return std::unique_ptr<process>(new process(pid, false, true)); }
    catch (...) { ptrace(PT_DETACH, pid, reinterpret_cast<caddr_t>(1), 0); throw; }
}
sdb::process::~process() {
    if (state_ != process_state::exited && state_ != process_state::terminated) {
        if (terminate_on_end_) {
            if (is_attached_) { ptrace(PT_KILL, pid_, nullptr, 0); ptrace(PT_CONTINUE, pid_, reinterpret_cast<caddr_t>(1), 0); }
            else kill(pid_, SIGKILL);

            int status; while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {}
        } else if (is_attached_) {
            try {
                if (state_ == process_state::running) { kill(pid_, SIGSTOP); wait_child(pid_); }
                breakpoint_sites_.for_each([](auto& b) { b.disable(); });
                watchpoints_.for_each([](auto& w) { w.disable(); });
            } catch (...) {}
            ptrace(PT_DETACH, pid_, reinterpret_cast<caddr_t>(1), 0);
            kill(pid_, SIGCONT);
        }
    }
    if (task_) mach_port_deallocate(mach_task_self(), task_);
void sdb::process::populate_existing_threads() {
    if (!task_) check(task_for_pid(mach_task_self(), pid_, &task_), "task_for_pid (sign sdb with its debugger entitlement)");
    check(task_threads(task_, &list, &count), "task_threads");
    for (unsigned i=0; i<count; ++i) {
        thread_identifier_info_data_t info{}; mach_msg_type_number_t size = THREAD_IDENTIFIER_INFO_COUNT;
        auto id = static_cast<pid_t>(info.thread_id);
        if(!main_thread_) main_thread_=id;
            ports_[id] = list[i];
            if (!ports_.count(current_thread_)) current_thread_ = id;
        } else mach_port_deallocate(mach_task_self(), list[i]);
}
void sdb::process::resume_all_threads(){resume();}
void sdb::process::step_over_breakpoint(pid_t t){if(breakpoint_sites_.enabled_stoppoint_at_address(get_pc(t)))step_instruction(t);}
sdb::stop_reason sdb::process::step_instruction(std::optional<pid_t> tid) {
    auto t=tid.value_or(current_thread_);current_thread_=t;
    if(breakpoint_sites_.enabled_stoppoint_at_address(get_pc(t))){bp=&breakpoint_sites_.get_by_address(get_pc(t));bp->disable();}
    for(auto [other,port]:ports_)if(other!=t){check(thread_suspend(port),"suspend other thread for step");suspended.push_back(port);}
    check(thread_set_state(ports_.at(t),ARM_DEBUG_STATE64,reinterpret_cast<thread_state_t>(&debug),ARM_DEBUG_STATE64_COUNT),"enable selected-thread step");
    trace(t==main_thread_?PT_STEP:PT_CONTINUE,pid_);state_=process_state::running;
    for(auto port:suspended)thread_resume(port);
    return reason;
sdb::stop_reason sdb::process::wait_on_signal(pid_t) {
    for(;;){
        if(state_!=process_state::stopped)return r;
        r.tid=current_thread_;
