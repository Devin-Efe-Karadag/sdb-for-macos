}
}
std::unique_ptr<sdb::target> sdb::target::launch(std::filesystem::path path,std::optional<int> output,const std::vector<std::string>& arguments){
    auto proc=process::launch(path,true,output,arguments);auto obj=create_loaded_elf(*proc,proc->executable_path());

    auto tgt=std::unique_ptr<target>(new target(std::move(proc),std::move(obj)));tgt->process_->set_target(tgt.get());

    auto entry=file_addr(*tgt->main_elf_,tgt->main_elf_->get_header().e_entry).to_virt_addr();
    auto& bp=tgt->process_->create_breakpoint_site(entry,false,true);bp.enable();
    tgt->process_->resume();auto reason=tgt->process_->wait_on_signal();
    tgt->process_->breakpoint_sites().remove_by_address(entry);

    if(reason.reason!=process_state::stopped)error::send("Program exited before its entry point");

    return tgt;
}
std::unique_ptr<sdb::target> sdb::target::attach(pid_t pid){
    auto proc=process::attach(pid);auto obj=create_loaded_elf(*proc,proc->executable_path());

    auto tgt=std::unique_ptr<target>(new target(std::move(proc),std::move(obj)));tgt->process_->set_target(tgt.get());tgt->resolve_dynamic_linker_rendezvous();
    tgt->notify_stop(stop_reason(tgt->process_->current_thread(),process_state::stopped,SIGSTOP));return tgt;
}

sdb::file_addr sdb::target::get_pc_file_address(
    std::optional<pid_t> otid) const {
    return process_->get_pc(otid).to_file_addr(elves_);
}

void sdb::target::notify_stop(const sdb::stop_reason& reason) {
    reload_dynamic_libraries();
    threads_.at(reason.tid).frames.unwind();
}

sdb::stop_reason sdb::target::step_in(std::optional<pid_t> otid) {
    auto tid = otid.value_or(process_->current_thread());
    auto& stack = get_stack(tid);
    auto& thread = threads_.at(tid);

    if (stack.inline_height() > 0) {
        stack.simulate_inlined_step_in();
        stop_reason reason(tid, process_state::stopped, SIGTRAP, trap_type::single_step);
        thread.state->reason = reason;

        return reason;
    }

    auto orig_line = line_entry_at_pc(tid);
    do {
        auto reason = process_->step_instruction(tid);

        if (!reason.is_step()) return reason;
    } while (line_entry_at_pc(tid) != line_table::iterator{} && ((orig_line != line_table::iterator{} && line_entry_at_pc(tid)->line == orig_line->line && line_entry_at_pc(tid)->file_entry == orig_line->file_entry)
        or line_entry_at_pc(tid)->end_sequence));

    auto pc = get_pc_file_address(tid);

    if (pc.elf_file() != nullptr) {
        auto& dwarf = pc.elf_file()->get_dwarf();

        auto func = dwarf.function_containing_address(pc);

        if (func and func->low_pc() == pc) {
            auto line = line_entry_at_pc();

            if (line != line_table::iterator{}) {
                ++line;

                return run_until_address(line->address.to_virt_addr(), tid);
            }
        }
    }

    stop_reason reason(
        tid, process_state::stopped, SIGTRAP, trap_type::single_step);
    thread.state->reason = reason;

    return reason;
}

sdb::line_table::iterator
sdb::target::line_entry_at_pc(std::optional<pid_t> otid) const {
    auto pc = get_pc_file_address(otid);

    if (!pc.elf_file()) return line_table::iterator();

    auto cu = pc.elf_file()->get_dwarf().compile_unit_containing_address(pc);

    if (!cu) return line_table::iterator();

    return cu->lines().get_entry_by_address(pc);
}

sdb::stop_reason sdb::target::run_until_address(
    virt_addr address, std::optional<pid_t> otid) {
    auto tid = otid.value_or(process_->current_thread());
    breakpoint_site* breakpoint_to_remove = nullptr;

    if (!process_->breakpoint_sites().contains_address(address)) {
        breakpoint_to_remove = &process_->create_breakpoint_site(
            address, false, true);
        breakpoint_to_remove->enable();
    }

    process_->resume(tid);

    auto reason = process_->wait_on_signal(tid);

    if (reason.is_breakpoint()
        and process_->get_pc(tid) == address) {
        reason.trap_reason = trap_type::single_step;
    }

    if (breakpoint_to_remove) {
        process_->breakpoint_sites().remove_by_address(
            breakpoint_to_remove->address());
    }

    threads_.at(tid).state->reason = reason;

    return reason;
}

sdb::stop_reason sdb::target::step_over(std::optional<pid_t> otid) {
    auto tid = otid.value_or(process_->current_thread());
    auto& thread = threads_.at(tid);
    auto& stack = get_stack(tid);

    auto orig_line = line_entry_at_pc(tid);
    disassembler disas(*process_);
    sdb::stop_reason reason;
    do {
        auto inline_stack = stack.inline_stack_at_pc();

        auto at_start_of_inline_frame = stack.inline_height() > 0;

        if (at_start_of_inline_frame) {
            auto frame_to_skip = inline_stack[inline_stack.size() - stack.inline_height()];

            auto return_address = frame_to_skip.high_pc().to_virt_addr();
            reason = run_until_address(return_address, tid);

            if (!reason.is_step()
                or process_->get_pc(tid) != return_address) {
                thread.state->reason = reason;

                return reason;
            }
        }
        else if (auto instructions = disas.disassemble(2, process_->get_pc(tid));
            (instructions[0].text.rfind("bl ",0)==0 || instructions[0].text.rfind("blr ",0)==0)) {
            reason = run_until_address(instructions[1].address);

            if (!reason.is_step()
                or process_->get_pc(tid) != instructions[1].address) {
                thread.state->reason = reason;

                return reason;
            }
        }
        else {
            reason = process_->step_instruction(tid);

            if (!reason.is_step()) {
                thread.state->reason = reason;

                return reason;
            }
        }
    } while (line_entry_at_pc(tid) != line_table::iterator{} && ((orig_line != line_table::iterator{} && line_entry_at_pc(tid)->line == orig_line->line && line_entry_at_pc(tid)->file_entry == orig_line->file_entry)
        or line_entry_at_pc(tid)->end_sequence));
    thread.state->reason = reason;

    return reason;
}

sdb::stop_reason sdb::target::step_out(std::optional<pid_t> otid) {
    auto tid = otid.value_or(process_->current_thread());
    auto& stack = get_stack(tid);

    auto inline_stack = stack.inline_stack_at_pc();

    auto has_inline_frames = inline_stack.size() > 1;

    auto at_inline_frame = stack.inline_height() < inline_stack.size() - 1;

    if (has_inline_frames and at_inline_frame) {
        auto current_frame = inline_stack[inline_stack.size() - stack.inline_height() - 1];

        auto return_address = current_frame.high_pc().to_virt_addr();

        return run_until_address(return_address, tid);
    }

    auto& regs = stack.frames()[stack.current_frame_index() + 1].regs;
    virt_addr return_address{ regs.read_by_id_as<std::uint64_t>(register_id::pc) };

    sdb::stop_reason reason;

    for (auto frames = stack.frames().size();
        stack.frames().size() >= frames;) {
        reason = run_until_address(return_address, tid);

        if (!reason.is_breakpoint()
            or process_->get_pc() != return_address) {
            return reason;
        }
    }

    return reason;
}

sdb::target::find_functions_result
sdb::target::find_functions(std::string name) const {
    find_functions_result result;

    elves_.for_each([&](auto& elf) {
        auto dwarf_found = elf.get_dwarf().find_functions(name);

        if (dwarf_found.empty()) {
            auto elf_found = elf.get_symbols_by_name(name);

            for (auto sym : elf_found) {
                result.elf_functions.push_back(std::pair{ &elf, sym });
            }
        }
        else {
            result.dwarf_functions.insert(
                result.dwarf_functions.end(),
                dwarf_found.begin(), dwarf_found.end());
        }
        });

    return result;
}

sdb::breakpoint&
sdb::target::create_address_breakpoint(
    virt_addr address, bool hardware, bool internal) {
    return breakpoints_.push(
        std::unique_ptr<address_breakpoint>(
            new address_breakpoint(
