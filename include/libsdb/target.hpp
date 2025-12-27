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
