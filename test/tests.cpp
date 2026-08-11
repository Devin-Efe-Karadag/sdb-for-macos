#include <libsdb/target.hpp>
#include <libsdb/disassembler.hpp>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
sdb::virt_addr symbol(sdb::target& t,const char* name){auto& image=t.get_main_elf();auto found=image.get_symbols_by_name(name);require(!found.empty(),"symbol missing");return sdb::file_addr(image,found[0]->st_value).to_virt_addr();}
int main(int argc,char** argv){try{
    require(argc==3,"expected fixture and case");std::string test=argv[2];

    if(test=="missing"){try{auto t=sdb::target::launch("/definitely/missing/sdb-target");}catch(const sdb::error&){return 0;}throw std::runtime_error("missing executable accepted");}

    if(test=="attach"){
        auto child=sdb::process::launch(argv[1],false);

        auto attached=sdb::process::attach(child->pid());
        require(attached->state()==sdb::process_state::stopped,"attach failed");

        return 0;
    }

    if(test=="arguments"){
        auto t=sdb::target::launch(argv[1],std::nullopt,{"first","second"});auto& p=t->get_process();p.resume();auto stop=p.wait_on_signal();require(stop.reason==sdb::process_state::exited&&stop.info==0,"arguments not forwarded");return 0;
    }

    auto t=sdb::target::launch(argv[1]);auto& p=t->get_process();

    if(test=="registers"){
        auto& r=p.get_registers();auto old=r.read_by_id_as<std::uint64_t>(sdb::register_id::x10);
        r.write_by_id(sdb::register_id::x10,std::uint64_t(0x123456789abcdef0));
        r.write_by_id(sdb::register_id::w10,std::uint32_t(0xaabbccdd));
        require(r.read_by_id_as<std::uint64_t>(sdb::register_id::x10)==0xaabbccdd,"w register did not zero extend");
        r.write_by_id(sdb::register_id::x10,old);

        auto addr=symbol(*t,"watched");long value=19;p.write_memory(addr,sdb::to_byte_span(value));require(p.read_memory_as<long>(addr)==19,"memory round trip");

        auto pc=p.get_pc();auto original=p.read_memory(pc,4);auto& bp=p.create_breakpoint_site(pc);bp.enable();require(p.read_memory(pc,4)!=original,"trap missing");require(p.read_memory_without_traps(pc,4)==original,"trap overlay incorrect");bp.disable();require(p.read_memory(pc,4)==original,"trap restoration incorrect");

        auto instructions=sdb::disassembler(p).disassemble(2);require(instructions.size()==2&&instructions[1].address.addr()==pc.addr()+4,"ARM64 disassembly");
        p.step_instruction();require(p.get_pc()!=pc,"instruction did not advance");
    }else if(test=="source"){
        auto& bp=t->create_function_breakpoint("inner");bp.enable();p.resume();auto stop=p.wait_on_signal();require(stop.is_breakpoint(),"function breakpoint missed");
        require(t->get_stack().frames().size()>=3,"missing stack frames");auto line=t->line_entry_at_pc()->line;t->step_over();require(t->line_entry_at_pc()->line!=line,"next did not advance source line");

        auto result=t->evaluate_expression("add(2,3)");require(result&&result->return_value.visualize(p)=="5","integer inferior call");

        auto floating=t->evaluate_expression("add_double(1.5,2.5)");require(floating&&floating->return_value.visualize(p)=="4","floating inferior call");

        auto many=t->evaluate_expression("many(1,2,3,4,5,6,7,8,9,10)");require(many&&many->return_value.visualize(p)=="55","stack arguments");

        auto pair=t->evaluate_expression("pair_add(pair_input)");require(pair&&sdb::from_bytes<double>(pair->return_value.data_ptr())==2.5&&sdb::from_bytes<double>(pair->return_value.data_ptr()+8)==4.5,"HFA argument/return");

        auto triple=t->evaluate_expression("triple_add(triple_input)");require(triple&&sdb::from_bytes<long>(triple->return_value.data_ptr()+16)==7,"indirect aggregate argument/return");
        t->step_out();require(t->function_name_at_address(p.get_pc()).find("outer")!=std::string::npos,"finish did not return to caller");
        p.resume();require(p.wait_on_signal().reason==sdb::process_state::exited,"process did not exit");
    }else if(test=="watchpoint"){
        auto addr=symbol(*t,"watched");auto& w=p.create_watchpoint(addr,sdb::stoppoint_mode::write,8);w.enable();p.resume();auto stop=p.wait_on_signal();
        require(stop.reason==sdb::process_state::stopped,"watchpoint missed");require(stop.trap_reason==sdb::trap_type::hardware_break,"watchpoint not classified");w.disable();p.resume();require(p.wait_on_signal().reason==sdb::process_state::exited,"watchpoint resume failed");
    }else if(test=="hardware"){
        auto addr=symbol(*t,"inner(int)");auto& bp=p.create_breakpoint_site(addr,true);bp.enable();p.resume();auto stop=p.wait_on_signal();require(stop.is_breakpoint()&&p.get_pc()==addr,"hardware breakpoint missed");bp.disable();p.resume();require(p.wait_on_signal().reason==sdb::process_state::exited,"hardware resume failed");
    }else if(test=="threads"){
        auto& bp=t->create_function_breakpoint("worker");bp.enable();p.resume();auto stop=p.wait_on_signal();require(stop.is_breakpoint(),"worker breakpoint missed");require(p.thread_states().size()>=2,"worker not enumerated");auto pc=p.get_pc();auto tid=p.current_thread();p.step_instruction();require(p.current_thread()==tid&&p.get_pc().addr()==pc.addr()+4,"worker stepping failed");bp.disable();p.resume();require(p.wait_on_signal().reason==sdb::process_state::exited,"threaded process did not exit");
    }else if(test=="syscall"){
        p.set_syscall_catch_policy(sdb::syscall_catch_policy::catch_some({20}));p.resume();auto entry=p.wait_on_signal();require(entry.syscall_info&&entry.syscall_info->entry&&entry.syscall_info->id==20,"syscall entry missing");p.resume();auto exit=p.wait_on_signal();require(exit.syscall_info&&!exit.syscall_info->entry&&exit.syscall_info->ret==p.pid(),"syscall return wrong");p.set_syscall_catch_policy(sdb::syscall_catch_policy::catch_none());p.resume();require(p.wait_on_signal().reason==sdb::process_state::exited,"syscall continue failed");
    }else if(test=="dynamic"){
        auto& bp=t->create_function_breakpoint("library_value");bp.enable();p.resume();auto stop=p.wait_on_signal();if(!stop.is_breakpoint())std::cerr<<"dynamic state="<<int(stop.reason)<<" info="<<int(stop.info)<<" trap="<<(stop.trap_reason?int(*stop.trap_reason):-1)<<" pc="<<std::hex<<p.get_pc().addr()<<std::dec<<"\n";require(stop.is_breakpoint(),"pending library breakpoint missed");require(t->function_name_at_address(p.get_pc()).find("library_value")!=std::string::npos,"wrong shared-library function");bp.disable();p.resume();require(p.wait_on_signal().reason==sdb::process_state::exited,"dlclose resume failed");
    }else throw std::runtime_error("unknown case");
    std::cout<<"PASS "<<test<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
