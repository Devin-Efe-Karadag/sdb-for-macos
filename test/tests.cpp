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
        auto result=t->evaluate_expression("add(2,3)");require(result&&result->return_value.visualize(p)=="5","integer inferior call");

        auto floating=t->evaluate_expression("add_double(1.5,2.5)");require(floating&&floating->return_value.visualize(p)=="4","floating inferior call");

        auto many=t->evaluate_expression("many(1,2,3,4,5,6,7,8,9,10)");require(many&&many->return_value.visualize(p)=="55","stack arguments");

        auto pair=t->evaluate_expression("pair_add(pair_input)");require(pair&&sdb::from_bytes<double>(pair->return_value.data_ptr())==2.5&&sdb::from_bytes<double>(pair->return_value.data_ptr()+8)==4.5,"HFA argument/return");

        auto triple=t->evaluate_expression("triple_add(triple_input)");require(triple&&sdb::from_bytes<long>(triple->return_value.data_ptr()+16)==7,"indirect aggregate argument/return");
        t->step_out();require(t->function_name_at_address(p.get_pc()).find("outer")!=std::string::npos,"finish did not return to caller");
