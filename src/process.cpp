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

    for (auto [id, port] : ports_) mach_port_deallocate(mach_task_self(), port);

    if (task_) mach_port_deallocate(mach_task_self(), task_);
}
void sdb::process::populate_existing_threads() {
    if (!is_attached_) return;

    if (!task_) check(task_for_pid(mach_task_self(), pid_, &task_), "task_for_pid (sign sdb with its debugger entitlement)");
    thread_act_array_t list; mach_msg_type_number_t count;
    check(task_threads(task_, &list, &count), "task_threads");

    std::unordered_set<pid_t> live;

    for (unsigned i=0; i<count; ++i) {
        thread_identifier_info_data_t info{}; mach_msg_type_number_t size = THREAD_IDENTIFIER_INFO_COUNT;

        auto k = thread_info(list[i], THREAD_IDENTIFIER_INFO, reinterpret_cast<thread_info_t>(&info), &size);

        if (k != KERN_SUCCESS || info.thread_id > std::numeric_limits<pid_t>::max()) { mach_port_deallocate(mach_task_self(), list[i]); continue; }

        auto id = static_cast<pid_t>(info.thread_id);
        live.insert(id);

        if(!main_thread_) main_thread_=id;

        if (!ports_.count(id)) {
            ports_[id] = list[i];
            threads_.emplace(id, thread_state{id, registers(*this,id)});

            if (!ports_.count(current_thread_)) current_thread_ = id;

            if (target_) report_thread_lifecycle_event(stop_reason(id, process_state::stopped, SIGSTOP));
        } else mach_port_deallocate(mach_task_self(), list[i]);
        read_all_registers(id);
    }
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(list), count*sizeof(thread_t));

    for (auto it=ports_.begin();it!=ports_.end();) {
        if (!live.count(it->first)) {
            report_thread_lifecycle_event(stop_reason(it->first, process_state::exited, 0));
            threads_.erase(it->first); mach_port_deallocate(mach_task_self(),it->second); it=ports_.erase(it);
        } else ++it;
    }

    if (!ports_.empty() && !ports_.count(current_thread_)) current_thread_=ports_.begin()->first;
}
void sdb::process::read_all_registers(pid_t tid) {
    auto& data=threads_.at(tid).regs.data_;
    mach_msg_type_number_t n=ARM_THREAD_STATE64_COUNT;
    check(thread_get_state(ports_.at(tid), ARM_THREAD_STATE64, reinterpret_cast<thread_state_t>(&data.regs), &n), "read ARM registers");
    n=ARM_NEON_STATE64_COUNT;
    check(thread_get_state(ports_.at(tid), ARM_NEON_STATE64, reinterpret_cast<thread_state_t>(&data.i387), &n), "read NEON registers");
}
sdb::registers& sdb::process::get_registers(std::optional<pid_t> tid) { return threads_.at(tid.value_or(current_thread_)).regs; }
const sdb::registers& sdb::process::get_registers(std::optional<pid_t> tid) const { return threads_.at(tid.value_or(current_thread_)).regs; }
void sdb::process::write_gprs(const user_regs_struct& r,std::optional<pid_t> tid) {
    check(thread_set_state(ports_.at(tid.value_or(current_thread_)),ARM_THREAD_STATE64,reinterpret_cast<thread_state_t>(const_cast<user_regs_struct*>(&r)),ARM_THREAD_STATE64_COUNT),"write ARM registers");
}
void sdb::process::write_fprs(const user_fpregs_struct& r,std::optional<pid_t> tid) {
    check(thread_set_state(ports_.at(tid.value_or(current_thread_)),ARM_NEON_STATE64,reinterpret_cast<thread_state_t>(const_cast<user_fpregs_struct*>(&r)),ARM_NEON_STATE64_COUNT),"write NEON registers");
}
void sdb::process::write_user_area(std::size_t offset,std::uint64_t value,std::optional<pid_t> tid) {
    auto& d=get_registers(tid).data_;

    if (offset+8>sizeof(d.regs)) error::send("Invalid register offset");
    memcpy(reinterpret_cast<char*>(&d)+offset,&value,8); write_gprs(d.regs,tid);
}
sdb::virt_addr sdb::process::get_pc(std::optional<pid_t> tid) const {return virt_addr(get_registers(tid).read_by_id_as<std::uint64_t>(register_id::pc));}
void sdb::process::set_pc(virt_addr pc,std::optional<pid_t> tid) {get_registers(tid).write_by_id(register_id::pc,pc.addr());}
std::vector<std::byte> sdb::process::read_memory(virt_addr addr,std::size_t size) const {
    std::vector<std::byte> data(size); if (!size) return data;
    mach_vm_size_t read=0;
    check(mach_vm_read_overwrite(task_,addr.addr(),size,reinterpret_cast<mach_vm_address_t>(data.data()),&read),"read memory");

    if (read!=size) error::send("Short memory read"); return data;
}
void sdb::process::write_memory(virt_addr addr,span<const std::byte> data) {
    std::size_t offset=0;

    while(offset<data.size()) {
        mach_vm_address_t region=addr.addr()+offset; mach_vm_size_t region_size=0;
        check(mach_vm_region_recurse(task_,&region,&region_size,&depth,reinterpret_cast<vm_region_recurse_info_t>(&info),&n),"find memory region");
                if(match&&!expecting_syscall_exit_){
                    syscall_information info{};info.id=id;info.entry=true;

                    for(int i=0;i<6;++i)info.args[i]=std::get<std::uint64_t>(get_registers(t).read(register_info_by_name("x"+std::to_string(i))));
                    expecting_syscall_exit_=true;queued_stop_=stop_reason(t,process_state::stopped,SIGTRAP,trap_type::syscall,info);break;
                }
                if(!reason.is_step()){queued_stop_=reason;break;}
            tracing_syscalls_=false;
        if(target_&&queued_stop_->reason==process_state::stopped)target_->notify_stop(*queued_stop_);
        return;
    }
    step_over_breakpoint(t);send_continue(t);
}
void sdb::process::resume_all_threads(){resume();}
void sdb::process::step_over_breakpoint(pid_t t){if(breakpoint_sites_.enabled_stoppoint_at_address(get_pc(t)))step_instruction(t);}
sdb::stop_reason sdb::process::step_instruction(std::optional<pid_t> tid) {
    auto t=tid.value_or(current_thread_);current_thread_=t;
    breakpoint_site* bp=nullptr;

    if(breakpoint_sites_.enabled_stoppoint_at_address(get_pc(t))){bp=&breakpoint_sites_.get_by_address(get_pc(t));bp->disable();}

    std::vector<mach_port_t> suspended;

    for(auto [other,port]:ports_)if(other!=t){check(thread_suspend(port),"suspend other thread for step");suspended.push_back(port);}

    auto debug=debug_state_;debug.__mdscr_el1|=1;
    check(thread_set_state(ports_.at(t),ARM_DEBUG_STATE64,reinterpret_cast<thread_state_t>(&debug),ARM_DEBUG_STATE64_COUNT),"enable selected-thread step");
    stepping_=true;
    trace(t==main_thread_?PT_STEP:PT_CONTINUE,pid_);state_=process_state::running;

    auto reason=wait_on_signal(t);stepping_=false;

    if(state_==process_state::stopped&&ports_.count(t))check(thread_set_state(ports_.at(t),ARM_DEBUG_STATE64,reinterpret_cast<thread_state_t>(&debug_state_),ARM_DEBUG_STATE64_COUNT),"disable selected-thread step");

    for(auto port:suspended)thread_resume(port);

    if(bp && state_==process_state::stopped)bp->enable();

    return reason;
}
sdb::stop_reason sdb::process::wait_on_signal(pid_t) {
    if(queued_stop_)return std::exchange(queued_stop_,std::nullopt).value();

    for(;;){
        stop_reason r(current_thread_,wait_child(pid_)); state_=r.reason;

        if(state_!=process_state::stopped)return r;
        populate_existing_threads();
        r.tid=current_thread_;

        if(r.info==SIGTRAP){
                }
            }

            for(auto& [tid,state]:threads_)if(breakpoint_sites_.enabled_stoppoint_at_address(get_pc(tid))){r.tid=tid;r.trap_reason=breakpoint_sites_.get_by_address(get_pc(tid)).is_hardware()?trap_type::hardware_break:trap_type::software_break;break;}
        }
        current_thread_=r.tid;threads_.at(r.tid).reason=r;

        for(auto& [t,s]:threads_)s.state=state_;

        if(r.info!=SIGTRAP&&r.info!=SIGSTOP)pending_signal_=r.info;

        if(r.trap_reason==trap_type::software_break){auto& bp=breakpoint_sites_.get_by_address(get_pc());if(bp.parent_&&bp.parent_->notify_hit()){resume();continue;}}

        if(target_&&!tracing_syscalls_)target_->notify_stop(r);

        return r;
    }
}
void sdb::process::report_thread_lifecycle_event(const stop_reason& r){if(thread_lifecycle_callback_)thread_lifecycle_callback_(r);if(target_)target_->notify_thread_lifecycle_event(r);}
std::filesystem::path sdb::process::executable_path() const {char path[PROC_PIDPATHINFO_MAXSIZE];if(proc_pidpath(pid_,path,sizeof(path))<=0)error::send_errno("proc_pidpath");return path;}
std::uint64_t sdb::process::image_load_address() const {
    mach_vm_address_t addr=0; mach_vm_size_t size=0;

    for(;;){vm_region_basic_info_data_64_t info{};mach_msg_type_number_t n=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object;
        auto k=mach_vm_region(task_,&addr,&size,VM_REGION_BASIC_INFO_64,reinterpret_cast<vm_region_info_t>(&info),&n,&object);if(k!=KERN_SUCCESS)break;

        if(object)mach_port_deallocate(mach_task_self(),object);
