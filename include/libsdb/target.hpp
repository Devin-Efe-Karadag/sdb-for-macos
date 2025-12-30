#ifndef SDB_TARGET_HPP
#define SDB_TARGET_HPP

#include <memory>
#include <libsdb/elf.hpp>
#include <libsdb/process.hpp>
#include <libsdb/stack.hpp>
#include <libsdb/dwarf.hpp>
#include <libsdb/breakpoint.hpp>
#include <libsdb/type.hpp>

namespace sdb {
    struct thread {
        thread(thread_state* state, stack frames)
            : state(state), frames(std::move(frames)) {}
        thread_state* state;
        stack frames;
    };

    class typed_data;

    class target {
    public:
        target() = delete;
        target(const target&) = delete;
        target& operator=(const target&) = delete;

        static std::unique_ptr<target> launch(
            std::filesystem::path path,

            std::optional<int> stdout_replacement = std::nullopt,

            const std::vector<std::string>& arguments = {});
        static std::unique_ptr<target> attach(pid_t pid);

        process& get_process() { return *process_; }

        const process& get_process() const { return *process_; }
        void notify_stop(const sdb::stop_reason& reason);
        file_addr get_pc_file_address(std::optional<pid_t> otid = std::nullopt) const;

        stack& get_stack(std::optional<pid_t> otid = std::nullopt) {
            auto tid = otid.value_or(process_->current_thread());

            return threads_.at(tid).frames;
        }

        const stack& get_stack(std::optional<pid_t> otid = std::nullopt) const {
            return const_cast<target*>(this)->get_stack(otid);
        }
        sdb::stop_reason step_in(std::optional<pid_t> otid = std::nullopt);
        sdb::stop_reason step_out(std::optional<pid_t> otid = std::nullopt);
        sdb::stop_reason step_over(std::optional<pid_t> otid = std::nullopt);
