#include <libsdb/target.hpp>
#include <libsdb/types.hpp>
#include <csignal>
#include <optional>
#include <libsdb/disassembler.hpp>
#include <libsdb/bit.hpp>
#include <cxxabi.h>
#include <fstream>
#include <libsdb/type.hpp>
#include <libsdb/parse.hpp>

#include <mach/mach_vm.h>
#include <libsdb/detail/arm64_abi.hpp>
#include <mach-o/dyld_images.h>
namespace {
std::unique_ptr<sdb::elf> create_loaded_elf(const sdb::process& proc,const std::filesystem::path& path){
    auto obj=std::make_unique<sdb::elf>(path);obj->notify_loaded(sdb::virt_addr(proc.image_load_address()-obj->preferred_base()));return obj;
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
                *this, address, hardware, internal)));
}

sdb::breakpoint&
sdb::target::create_function_breakpoint(
    std::string function_name, bool hardware, bool internal) {
    return breakpoints_.push(
        std::unique_ptr<function_breakpoint>(
            new function_breakpoint(
                *this, function_name, hardware, internal)));
}

sdb::breakpoint&
sdb::target::create_line_breakpoint(
    std::filesystem::path file, std::size_t line,

    bool hardware, bool internal) {
    return breakpoints_.push(
        std::unique_ptr<line_breakpoint>(
            new line_breakpoint(
                *this, file, line, hardware, internal)));
}

std::string sdb::target::function_name_at_address(virt_addr address) const {
    auto file_address = address.to_file_addr(elves_);

    auto obj = file_address.elf_file();

    if (!obj) return "";

    auto func = obj->get_dwarf().function_containing_address(file_address);

    auto elf_filename = obj->path().filename().string();

    std::string func_name = "";

    if (func and func->name()) {
        func_name = *func->name();
    }
    else if (auto elf_func = obj->get_symbol_containing_address(file_address);
        elf_func and ELF64_ST_TYPE(elf_func.value()->st_info) == STT_FUNC) {
        func_name = obj->get_string(elf_func.value()->st_name);
    }

    if (!func_name.empty()) {
        return elf_filename + "`" + func_name;
    }

    return "";
}

void sdb::target::resolve_dynamic_linker_rendezvous(){reload_dynamic_libraries();}

std::vector<sdb::line_table::iterator> sdb::target::get_line_entries_by_line(
    std::filesystem::path path, std::size_t line) const {
    std::vector<sdb::line_table::iterator> entries;
    elves_.for_each([&](auto& elf) {
        for (auto& cu : elf.get_dwarf().compile_units()) {
            auto new_entries = cu->lines().get_entries_by_line(path, line);
            entries.insert(entries.end(), new_entries.begin(), new_entries.end());
        }
        });
    return entries;
}

void sdb::target::reload_dynamic_libraries(){
    task_dyld_info_data_t task_info_data{};mach_msg_type_number_t n=TASK_DYLD_INFO_COUNT;

    if(task_info(process_->task_port(),TASK_DYLD_INFO,reinterpret_cast<task_info_t>(&task_info_data),&n)!=KERN_SUCCESS)error::send("Cannot read dyld information");

    if(!task_info_data.all_image_info_addr)return;

    auto info=process_->read_memory_as<dyld_all_image_infos>(virt_addr(task_info_data.all_image_info_addr));

    if(!dynamic_linker_rendezvous_address_.addr()&&info.notification){
        auto address=virt_addr(reinterpret_cast<std::uint64_t>(info.notification)&0x0000ffffffffffffULL);
        dynamic_linker_rendezvous_address_=address;
        auto& bp=create_address_breakpoint(address,false,true);
        bp.install_hit_handler([this]{reload_dynamic_libraries();return true;});bp.enable();
    }

    if(!info.infoArray||info.infoArrayCount>65536)return;

    for(unsigned i=0;i<info.infoArrayCount;++i){
        auto image=process_->read_memory_as<dyld_image_info>(virt_addr(reinterpret_cast<std::uint64_t>(info.infoArray)+i*sizeof(dyld_image_info)));

        auto path=std::filesystem::path(process_->read_string(virt_addr(reinterpret_cast<std::uint64_t>(image.imageFilePath))));

        if(!std::filesystem::is_regular_file(path))continue;
        path=std::filesystem::canonical(path);

        if(elves_.get_elf_by_path(path))continue;

        auto obj=std::make_unique<elf>(path);obj->notify_loaded(virt_addr(reinterpret_cast<std::uint64_t>(image.imageLoadAddress)-obj->preferred_base()));elves_.push(std::move(obj));
    }
    breakpoints_.for_each([](auto& b){b.resolve();});
}

void sdb::target::notify_thread_lifecycle_event(
    const stop_reason& reason) {
    auto tid = reason.tid;

    if (reason.reason == process_state::stopped) {
        auto& state = process_->thread_states()[tid];
        threads_.emplace(
            tid, thread{ &state, stack{this, tid} });
    }
    else {
        threads_.erase(tid);
    }
}

std::vector<std::byte> sdb::target::read_location_data(
    const dwarf_expression::result& loc, std::size_t size,

    std::optional<pid_t> otid) const {
    auto tid = otid.value_or(process_->current_thread());

    if (auto simple_loc = std::get_if<sdb::dwarf_expression::simple_location>(&loc)) {
        if (auto reg_loc = std::get_if<sdb::dwarf_expression::register_result>(simple_loc)) {
            auto reg_info = register_info_by_dwarf(reg_loc->reg_num);

            auto reg_value = threads_.at(tid).frames.current_frame().regs.read(reg_info);

            auto get_bytes = [](auto value) {
                std::vector<std::byte> bytes(sizeof(value));

                auto begin = reinterpret_cast<const std::byte*>(&value);

                std::copy(begin, begin + sizeof(value), bytes.data());

                return bytes;
                };
            return std::visit(get_bytes, reg_value);
        }
        else if (
            auto addr_res = std::get_if<sdb::dwarf_expression::address_result>(simple_loc)) {
            return process_->read_memory(addr_res->address, size);
        }
        else if (auto data_res = std::get_if<sdb::dwarf_expression::data_result>(simple_loc)) {
            return { data_res->data.begin(), data_res->data.end() };
        }
        else if (
            auto literal_res = std::get_if<sdb::dwarf_expression::literal_result>(simple_loc)) {
            auto begin = reinterpret_cast<const std::byte*>(&literal_res->value);

            return { begin, begin + size };
        }
    }
    else if (auto pieces_res = std::get_if<sdb::dwarf_expression::pieces_result>(&loc)) {
        std::vector<std::byte> data(size);

        std::size_t offset = 0;

        for (auto& piece : pieces_res->pieces) {
            auto byte_size = (piece.bit_size + 7) / 8;

            auto piece_data = read_location_data(piece.location, byte_size, otid);

            if (offset % 8 == 0 and piece.offset == 0 and piece.bit_size % 8 == 0) {
                std::copy(piece_data.begin(), piece_data.end(), data.begin() + offset / 8);
                offset += piece.bit_size;
            }
            else {
                auto dest = reinterpret_cast<std::uint8_t*>(data.data());

                auto src = reinterpret_cast<const std::uint8_t*>(piece_data.data());
                memcpy_bits(dest, 0, src, piece.offset, piece.bit_size);
            }
        }

        return data;
    }
    sdb::error::send("Invalid location type");
}

std::optional<sdb::die> sdb::target::find_variable(
    std::string name, sdb::file_addr pc) const {
    auto& dwarf = pc.elf_file()->get_dwarf();

    auto local = dwarf.find_local_variable(name, pc);

    if (local) return local;

    std::optional<die> global = std::nullopt;
    elves_.for_each([&](auto& elf) {
        auto& dwarf = elf.get_dwarf();

        auto found = dwarf.find_global_variable(name);

        if (found) {
            global = *found;
        }
        });
    return global;
}

namespace {
    sdb::typed_data get_initial_variable_data(
        const sdb::target& target, std::string name, sdb::file_addr pc) {
        if (name[0] == '$') {
            auto index = sdb::to_integral<std::size_t>(name.substr(1));

            if (!index) {
                sdb::error::send("Invalid expression result index");
            }

            return target.get_expression_result(*index);
        }

        auto var = target.find_variable(name, pc);

        if (!var) {
            sdb::error::send("Variable not found");
        }

        auto var_type = var.value()[DW_AT_type].as_type();

        auto loc = var.value()[DW_AT_location].as_evaluated_location(
            target.get_process(), target.get_stack().current_frame().regs, false);
        auto data_vec = target.read_location_data(loc, var_type.byte_size());

        std::optional<sdb::virt_addr> address;

        if (auto single_loc = std::get_if<sdb::dwarf_expression::simple_location>(&loc)) {
            if (auto addr_res = std::get_if<sdb::dwarf_expression::address_result>(single_loc)) {
                address = addr_res->address;
            }
        }

        return { std::move(data_vec), var_type, address };
    }

    sdb::typed_data parse_argument(
        sdb::target& target, pid_t tid, std::string_view arg) {
        if (arg.empty()) {
            sdb::error::send("Empty argument");
        }

        if (arg.size() > 2 and arg[0] == '"' and arg[arg.size() - 1] == '"') {
            auto ptr = target.inferior_malloc(arg.size() - 1);

            std::string arg_str{ arg.substr(1, arg.size() - 2) };

            auto data_ptr = reinterpret_cast<const std::byte*>(arg_str.data());
            sdb::span<const std::byte> data = {
                data_ptr, arg_str.size() + 1 };
            target.get_process().write_memory(ptr, data);

            return { sdb::to_byte_vec(ptr), sdb::builtin_type::string };
        }
        else if (arg == "true" or arg == "false") {
            auto value = arg == "true";

            return { sdb::to_byte_vec(value), sdb::builtin_type::boolean };
        }
        else if (arg[0] == '\'') {
            if (arg.size() != 3 or arg[2] != '\'') {
                sdb::error::send("Invalid character literal");
            }

            return { sdb::to_byte_vec(arg[1]), sdb::builtin_type::character };
        }
        else if (arg[0] == '-' or std::isdigit(arg[0])) {
            if (arg.find(".") != std::string::npos) {
                auto value = sdb::to_float<double>(arg);

                if (!value) {
                    sdb::error::send("Invalid floating point literal");
                }

                return { sdb::to_byte_vec(*value), sdb::builtin_type::floating_point };
            }
            else {
                auto value = sdb::to_integral<std::int64_t>(arg);

                if (!value) {
                    sdb::error::send("Invalid integer literal");
                }

                return { sdb::to_byte_vec(*value), sdb::builtin_type::integer };
            }
        }
        else {
            auto pc = target.get_pc_file_address(tid);

            auto res = target.resolve_indirect_name(std::string(arg), pc);

            if (!res.funcs.empty()) {
                sdb::error::send("Nested function calls not supported");
            }

            return *res.variable;
        }
    }

    std::vector<sdb::typed_data> collect_arguments(
        sdb::target& target, pid_t tid, std::string_view arg_string,

        const std::vector<sdb::die>& funcs,

        std::optional<sdb::typed_data> object) {
        std::vector<sdb::typed_data> args;
        auto& proc = target.get_process();

        if (object) {
            std::vector<std::byte> data;

            if (object->address()) {
                data = sdb::to_byte_vec(*object->address());
            }
            else {
                auto& regs = proc.get_registers(tid);

                auto rsp = regs.read_by_id_as<std::uint64_t>(sdb::register_id::sp);
                rsp -= object->value_type().byte_size();
                proc.write_memory(sdb::virt_addr{ rsp }, object->data());
                regs.write_by_id(sdb::register_id::sp, rsp, true);
                data = sdb::to_byte_vec(rsp);
            }

            auto obj_ptr_die = funcs[0][DW_AT_object_pointer].as_reference();

            auto this_type = obj_ptr_die[DW_AT_type].as_type();
            args.push_back({ std::move(data), this_type });
        }

        auto args_start = 1;

        auto args_end = arg_string.find(')');

        while (args_start < args_end) {
            auto comma_pos = arg_string.find(',', args_start);

            if (comma_pos == std::string::npos) {
                comma_pos = args_end;
            }

            auto arg_expr = arg_string.substr(args_start, comma_pos - args_start);
            args.push_back(parse_argument(target, tid, arg_expr));
            args_start = comma_pos + 1;
        }

        return args;
    }

    sdb::die resolve_overload(
        const std::vector<sdb::die>& funcs,

        const std::vector<sdb::typed_data>& args) {
        std::optional<sdb::die> matching_func;

        for (auto& func : funcs) {
            bool matches = true;

            auto arg_it = args.begin();

            auto params = func.parameter_types();

            if (args.size() == params.size()) {
                for (auto param_it = params.begin();
                    arg_it != args.end();
                    ++param_it, ++arg_it) {
                    if (*param_it != arg_it->value_type()) {
                        matches = false;
                        break;
                    }
                }
            }
            else {
                matches = false;
            }

            if (matches) {
                if (matching_func) sdb::error::send("Ambiguous function call");
                matching_func = func;
            }
        }

        if (!matching_func) sdb::error::send("No matching function");

        return *matching_func;
    }

    void setup_arguments(sdb::target& target,sdb::die func,std::vector<sdb::typed_data> args,sdb::registers& regs,std::optional<sdb::virt_addr> ret){
        auto params=func.parameter_types();unsigned integer=0,floating=0;auto sp=regs.read_by_id_as<std::uint64_t>(sdb::register_id::sp);

        struct stack_value{std::vector<std::byte> bytes;std::size_t alignment;};std::vector<stack_value> stack;

        if(ret)regs.write_by_id(sdb::register_id::x8,ret->addr());

        for(std::size_t i=0;i<params.size();++i){
            auto type=params[i];auto size=type.byte_size();auto bytes=args[i].data();std::vector<std::byte> value(bytes.begin(),bytes.end());

            if(type.is_reference_type()){
                auto address=args[i].address();if(!address){auto slot=target.inferior_malloc(value.size());target.get_process().write_memory(slot,value);address=slot;}
                value=sdb::to_byte_vec(address->addr());size=8;
            }

            auto hfa=sdb::classify_arm64_hfa(type);bool fp=hfa.has_value();

            if(fp&&hfa->element_size==4&&hfa->offsets.size()==1&&args[i].value_type().byte_size()==8){float f=static_cast<float>(sdb::from_bytes<double>(value.data()));value=sdb::to_byte_vec(f);}

            if(fp&&floating+hfa->offsets.size()<=8){for(auto offset:hfa->offsets){sdb::byte128 v{};std::copy_n(value.begin()+offset,hfa->element_size,v.begin());regs.write(sdb::register_info_by_name("v"+std::to_string(floating++)),v);}}
            else if(!fp&&size<=16&&integer+(size+7)/8<=8){for(std::size_t j=0;j<size;j+=8){std::uint64_t v=0;memcpy(&v,value.data()+j,std::min<std::size_t>(8,size-j));regs.write(sdb::register_info_by_name("x"+std::to_string(integer++)),v);}}
            else{if(!fp&&size>16){auto slot=target.inferior_malloc(size);target.get_process().write_memory(slot,value);value=sdb::to_byte_vec(slot.addr());size=8;if(integer<8){regs.write(sdb::register_info_by_name("x"+std::to_string(integer++)),slot.addr());continue;}}stack.push_back({std::move(value),std::max<std::size_t>(1,std::min<std::size_t>(type.alignment(),16))});}
        }

        std::size_t count=0;for(auto& arg:stack){count=(count+arg.alignment-1)&~(arg.alignment-1);count+=arg.bytes.size();}
        sp=(sp-count)&~15ULL;auto at=sp;for(auto& arg:stack){at=(at+arg.alignment-1)&~(arg.alignment-1);target.get_process().write_memory(sdb::virt_addr(at),arg.bytes);at+=arg.bytes.size();}regs.write_by_id(sdb::register_id::sp,sp);
    }
    sdb::typed_data read_return_value(sdb::target& target,sdb::die func,sdb::virt_addr slot,sdb::registers& regs){
        auto type=func[DW_AT_type].as_type();auto size=type.byte_size();std::vector<std::byte> bytes(size);

        auto hfa=sdb::classify_arm64_hfa(type);

        if(hfa){unsigned i=0;for(auto offset:hfa->offsets){auto v=std::get<sdb::byte128>(regs.read(sdb::register_info_by_name("v"+std::to_string(i++))));std::copy_n(v.begin(),hfa->element_size,bytes.begin()+offset);}}
        else if(size>16)bytes=target.get_process().read_memory(slot,size);
        else{auto x0=regs.read_by_id_as<std::uint64_t>(sdb::register_id::x0);auto x1=regs.read_by_id_as<std::uint64_t>(sdb::register_id::x1);memcpy(bytes.data(),&x0,std::min<std::size_t>(size,8));if(size>8)memcpy(bytes.data()+8,&x1,size-8);}
        target.get_process().write_memory(slot,bytes);return {std::move(bytes),type,slot};
    }

    std::optional<sdb::typed_data> inferior_call_from_dwarf(
        sdb::target& target, sdb::die func,

        const std::vector<sdb::typed_data>& args,
        sdb::virt_addr return_addr, pid_t tid) {
        auto& regs = target.get_process().get_registers(tid);

        auto saved_regs = regs;

        sdb::virt_addr call_addr;

        if (func.contains(DW_AT_low_pc) or func.contains(DW_AT_ranges)) {
            call_addr = func.low_pc().to_virt_addr();
        }
        else {
            auto def = func.cu()->dwarf_info()->get_member_function_definition(func);

            if (!def) {
                sdb::error::send("No function definition found");
            }
            call_addr = def->low_pc().to_virt_addr();
        }

        std::optional<sdb::virt_addr> return_slot;

        if (func.contains(DW_AT_type)) {
            auto ret_type = func[DW_AT_type].as_type();
            return_slot = target.inferior_malloc(ret_type.byte_size());
        }

        setup_arguments(target, func, args, regs, return_slot);

        auto new_regs = target.get_process().inferior_call(
            call_addr, return_addr, saved_regs, tid);

        if (func.contains(DW_AT_type)) {
            return read_return_value(
                target, func, *return_slot, new_regs);
        }

        return std::nullopt;
    }
}

sdb::target::resolve_indirect_name_result
sdb::target::resolve_indirect_name(
    std::string name, sdb::file_addr pc) const {
    auto op_pos = name.find_first_of(".-[(");

    if (name[op_pos] == '(') {
        auto func_name = name.substr(0, op_pos);

        auto funcs = find_functions(func_name);

        return { std::nullopt, std::move(funcs.dwarf_functions) };
    }

    auto var_name = name.substr(0, op_pos);
    auto& dwarf = pc.elf_file()->get_dwarf();

    auto data = get_initial_variable_data(*this, var_name, pc);

    while (op_pos != std::string::npos) {
        if (name[op_pos] == '-') {
            if (name[op_pos + 1] != '>') {
                sdb::error::send("Invalid operator");
            }
            data = data.deref_pointer(get_process());
            op_pos++;
        }

        if (name[op_pos] == '.' or name[op_pos] == '>') {
            auto member_name_start = op_pos + 1;
            op_pos = name.find_first_of(".-[(,", member_name_start);

            auto member_name = name.substr(member_name_start, op_pos - member_name_start);

            if (name[op_pos] == '(') {
                std::vector<die> funcs;

                auto stripped_value_type = data.value_type().strip_cvref_typedef();

                for (auto& child : stripped_value_type.get_die().children()) {
                    if (child.abbrev_entry()->tag == DW_TAG_subprogram and
                        child.contains(DW_AT_object_pointer) and
                        child.name() == member_name) {
                        funcs.push_back(child);
                    }
                }

                if (funcs.empty()) {
                    sdb::error::send("No such member function");
                }

                return { std::move(data), std::move(funcs) };
            }
            data = data.read_member(get_process(), member_name);
            name = name.substr(member_name_start);
        }
        else if (name[op_pos] == '[') {
            auto int_end = name.find(']', op_pos);

            auto index_str = name.substr(op_pos + 1, int_end - op_pos - 1);

            auto index = to_integral<std::size_t>(index_str);

            if (!index) {
                sdb::error::send("Invalid index");
            }
            data = data.index(get_process(), *index);
            name = name.substr(int_end + 1);
        }
        op_pos = name.find_first_of(".-[(");
    }

    return { std::move(data), {} };
}

sdb::virt_addr sdb::target::inferior_malloc(std::size_t size){
    mach_vm_address_t address=0;

    if(mach_vm_allocate(process_->task_port(),&address,std::max<std::size_t>(size,1),VM_FLAGS_ANYWHERE)!=KERN_SUCCESS)error::send("Cannot allocate inferior expression storage");

    return virt_addr(address);
}

std::optional<sdb::target::evaluate_expression_result>
sdb::target::evaluate_expression(
    std::string_view expr, std::optional<pid_t> otid) {
    auto tid = otid.value_or(process_->current_thread());

    auto pc = get_pc_file_address(tid);

    auto paren_pos = expr.find('(');

    if (paren_pos == std::string::npos) {
        sdb::error::send("Invalid expression");
    }

    std::string name{ expr.substr(0, paren_pos + 1) };

    auto [variable, funcs] = resolve_indirect_name(name, pc);

    if (funcs.empty()) {
        sdb::error::send("Invalid expression");
    }

    auto entry_point = virt_addr{ file_addr(*main_elf_,main_elf_->get_header().e_entry).to_virt_addr().addr() };

    auto arg_string = expr.substr(paren_pos);

    auto args = collect_arguments(
        *this, tid, arg_string, funcs, variable);
    auto func = resolve_overload(funcs, args);

    auto ret = inferior_call_from_dwarf(
        *this, func, args, entry_point, tid);
    if (ret) {
        expression_results_.push_back(*ret);

        return evaluate_expression_result{
            std::move(*ret), expression_results_.size() - 1
        };
    }

    return std::nullopt;
}

const sdb::typed_data& sdb::target::get_expression_result(
    std::size_t i) const {
    auto& res = expression_results_[i];

    auto new_data = process_->read_memory(
        *res.address(), res.value_type().byte_size());
    res = typed_data{
        std::move(new_data), res.value_type(), res.address() };
    return res;
}
