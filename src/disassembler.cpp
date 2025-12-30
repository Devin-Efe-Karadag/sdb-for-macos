#include <libsdb/disassembler.hpp>
#include <llvm-c/Disassembler.h>
#include <llvm-c/Target.h>
#include <memory>
std::vector<sdb::disassembler::instruction> sdb::disassembler::disassemble(std::size_t n,std::optional<virt_addr> addr){
    static const bool initialized=[] {LLVMInitializeAArch64TargetInfo();LLVMInitializeAArch64TargetMC();LLVMInitializeAArch64Disassembler();return true;}();(void)initialized;

    auto ctx=LLVMCreateDisasm("arm64-apple-macos",nullptr,0,nullptr,nullptr);

    if(!ctx)error::send("Cannot initialize ARM64 disassembler");

    std::unique_ptr<void,decltype(&LLVMDisasmDispose)> owner(ctx,LLVMDisasmDispose);

    auto pc=addr.value_or(process_->get_pc());std::vector<instruction> result;

    for(std::size_t i=0;i<n;++i){auto bytes=process_->read_memory_without_traps(pc,4);char text[256];auto count=LLVMDisasmInstruction(ctx,reinterpret_cast<std::uint8_t*>(bytes.data()),4,pc.addr(),text,sizeof text);
        std::string value=count?text:".word (unknown instruction)";auto start=value.find_first_not_of(" \t");if(start!=std::string::npos)value.erase(0,start);for(auto& c:value)if(c=='\t')c=' ';result.push_back({pc,value});pc+=4;
    }return result;
}
