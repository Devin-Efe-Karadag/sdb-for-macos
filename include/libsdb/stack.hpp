#ifndef SDB_STACK_HPP
#define SDB_STACK_HPP

#include <vector>
#include <libsdb/dwarf.hpp>
#include <libsdb/types.hpp>
#include <libsdb/registers.hpp>

namespace sdb {
    class target;

    struct stack_frame {
        registers regs;
        virt_addr backtrace_report_address;
        die func_die;

        bool inlined = false;
        source_location location;
    };

    class stack {
    public:
        stack(target* tgt) : target_(tgt) {}
        void reset_inline_height();

        std::vector<sdb::die> inline_stack_at_pc() const;

        std::uint32_t inline_height() const { return inline_height_; }

        const target& get_target() const { return *target_; }
        void simulate_inlined_step_in() {
            --inline_height_;
            current_frame_ = inline_height_;
        }

        void unwind();
        void up() { if(current_frame_ + 1 >= frames_.size()) error::send("Already at oldest frame"); ++current_frame_; }
        void down() { if(current_frame_ <= inline_height_) error::send("Already at youngest frame"); --current_frame_; }

        span<const stack_frame> frames() const;
