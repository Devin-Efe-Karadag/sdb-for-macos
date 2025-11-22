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
