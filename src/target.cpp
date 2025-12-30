}
std::unique_ptr<sdb::target> sdb::target::launch(std::filesystem::path path,std::optional<int> output,const std::vector<std::string>& arguments){
    auto tgt=std::unique_ptr<target>(new target(std::move(proc),std::move(obj)));tgt->process_->set_target(tgt.get());
    auto& bp=tgt->process_->create_breakpoint_site(entry,false,true);bp.enable();
    tgt->process_->breakpoint_sites().remove_by_address(entry);
    return tgt;
std::unique_ptr<sdb::target> sdb::target::attach(pid_t pid){
    auto tgt=std::unique_ptr<target>(new target(std::move(proc),std::move(obj)));tgt->process_->set_target(tgt.get());tgt->resolve_dynamic_linker_rendezvous();
}
    std::optional<pid_t> otid) const {
}
    reload_dynamic_libraries();
}
    auto tid = otid.value_or(process_->current_thread());
    auto& thread = threads_.at(tid);
        stack.simulate_inlined_step_in();
        thread.state->reason = reason;
    }
    do {
        if (!reason.is_step()) return reason;
        or line_entry_at_pc(tid)->end_sequence));
    if (pc.elf_file() != nullptr) {
        auto func = dwarf.function_containing_address(pc);
            auto line = line_entry_at_pc();
                ++line;
            }
    }
        tid, process_state::stopped, SIGTRAP, trap_type::single_step);
    return reason;
sdb::line_table::iterator
    auto pc = get_pc_file_address(otid);
    auto cu = pc.elf_file()->get_dwarf().compile_unit_containing_address(pc);
    return cu->lines().get_entry_by_address(pc);
sdb::stop_reason sdb::target::run_until_address(
    auto tid = otid.value_or(process_->current_thread());
    if (!process_->breakpoint_sites().contains_address(address)) {
            address, false, true);
    }
    auto reason = process_->wait_on_signal(tid);
        and process_->get_pc(tid) == address) {
    }
        process_->breakpoint_sites().remove_by_address(
    }
    return reason;
sdb::stop_reason sdb::target::step_over(std::optional<pid_t> otid) {
    auto& thread = threads_.at(tid);
    auto orig_line = line_entry_at_pc(tid);
    sdb::stop_reason reason;
        auto inline_stack = stack.inline_stack_at_pc();
        if (at_start_of_inline_frame) {
            reason = run_until_address(return_address, tid);

            if (!reason.is_step()
                thread.state->reason = reason;
            }
        else if (auto instructions = disas.disassemble(2, process_->get_pc(tid));
            reason = run_until_address(instructions[1].address);
                or process_->get_pc(tid) != instructions[1].address) {
                return reason;
        }
            reason = process_->step_instruction(tid);
                thread.state->reason = reason;
            }
    } while (line_entry_at_pc(tid) != line_table::iterator{} && ((orig_line != line_table::iterator{} && line_entry_at_pc(tid)->line == orig_line->line && line_entry_at_pc(tid)->file_entry == orig_line->file_entry)
    thread.state->reason = reason;
}
    auto tid = otid.value_or(process_->current_thread());
    auto inline_stack = stack.inline_stack_at_pc();
    auto at_inline_frame = stack.inline_height() < inline_stack.size() - 1;
        auto current_frame = inline_stack[inline_stack.size() - stack.inline_height() - 1];
        return run_until_address(return_address, tid);
    auto& regs = stack.frames()[stack.current_frame_index() + 1].regs;
    sdb::stop_reason reason;
        stack.frames().size() >= frames;) {
        if (!reason.is_breakpoint()
            return reason;
    }
}
sdb::target::find_functions(std::string name) const {
    elves_.for_each([&](auto& elf) {
        if (dwarf_found.empty()) {
            for (auto sym : elf_found) {
            }
        else {
                result.dwarf_functions.end(),
        }
    return result;
}

sdb::breakpoint&
sdb::target::create_address_breakpoint(
    virt_addr address, bool hardware, bool internal) {
    return breakpoints_.push(
        std::unique_ptr<address_breakpoint>(
            new address_breakpoint(
