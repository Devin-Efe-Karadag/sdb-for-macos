#include <libsdb/detail/macos_unwind.hpp>
#include <libsdb/process.hpp>
#include <libsdb/elf.hpp>
#include <mach-o/compact_unwind_encoding.h>
namespace {
template<class T>T read(sdb::span<const std::byte> bytes,std::size_t offset){
    if(offset>bytes.size()||sizeof(T)>bytes.size()-offset)sdb::error::send("Truncated compact unwind info");

    return sdb::from_bytes<T>(bytes.begin()+offset);
}
}
std::optional<sdb::registers> sdb::unwind_compact(const process& proc,file_addr pc,registers& regs){
    auto& image=*pc.elf_file();auto bytes=image.get_section_contents(".unwind_info");if(!bytes.size())return std::nullopt;

    auto header=read<unwind_info_section_header>(bytes,0);if(header.version!=1)error::send("Unsupported compact unwind version");

    auto relative=pc.addr()-image.preferred_base();

    std::uint32_t encoding=0;std::uint64_t start=0;bool found=false;

    for(std::uint32_t i=0;i+1<header.indexCount;++i){
        auto index=read<unwind_info_section_header_index_entry>(bytes,header.indexSectionOffset+i*sizeof(unwind_info_section_header_index_entry));

        auto next=read<unwind_info_section_header_index_entry>(bytes,header.indexSectionOffset+(i+1)*sizeof(unwind_info_section_header_index_entry));

        if(relative<index.functionOffset||relative>=next.functionOffset)continue;

        auto page=index.secondLevelPagesSectionOffset;if(!page)return std::nullopt;auto kind=read<std::uint32_t>(bytes,page);

        if(kind==UNWIND_SECOND_LEVEL_REGULAR){
            auto p=read<unwind_info_regular_second_level_page_header>(bytes,page);

            for(unsigned j=0;j<p.entryCount;++j){auto e=read<unwind_info_regular_second_level_entry>(bytes,page+p.entryPageOffset+j*sizeof(unwind_info_regular_second_level_entry));if(relative<e.functionOffset)break;encoding=e.encoding;start=e.functionOffset;found=true;}
        }else if(kind==UNWIND_SECOND_LEVEL_COMPRESSED){
            auto p=read<unwind_info_compressed_second_level_page_header>(bytes,page);

            for(unsigned j=0;j<p.entryCount;++j){auto e=read<std::uint32_t>(bytes,page+p.entryPageOffset+j*4);auto address=index.functionOffset+(e&0xffffff);if(relative<address)break;
                unsigned key=e>>24;

                if(key<header.commonEncodingsArrayCount)encoding=read<std::uint32_t>(bytes,header.commonEncodingsArraySectionOffset+key*4);
                else{key-=header.commonEncodingsArrayCount;if(key>=p.encodingsCount)error::send("Invalid compact unwind encoding index");encoding=read<std::uint32_t>(bytes,page+p.encodingsPageOffset+key*4);}
                start=address;found=true;
            }
        }else error::send("Invalid compact unwind page kind");
        break;
    }

    if(!found)return std::nullopt;

    auto mode=encoding&UNWIND_ARM64_MODE_MASK;if(mode==UNWIND_ARM64_MODE_DWARF)return std::nullopt;

    auto out=regs;auto sp=regs.read_by_id_as<std::uint64_t>(register_id::sp);auto fp=regs.read_by_id_as<std::uint64_t>(register_id::fp);

    auto ret=regs.read_by_id_as<std::uint64_t>(register_id::lr);std::uint64_t cfa=sp;

    if(relative==start){ /* The callee's prologue has not run yet. */ }
    else if(mode==UNWIND_ARM64_MODE_FRAMELESS)cfa=sp+((encoding&UNWIND_ARM64_FRAMELESS_STACK_SIZE_MASK)>>12)*16;
    else if(mode==UNWIND_ARM64_MODE_FRAME){
        if(!fp||fp%16)error::send("Invalid frame pointer");
        cfa=fp+16;ret=proc.read_memory_as<std::uint64_t>(virt_addr(fp+8));
        out.write_by_id(register_id::fp,proc.read_memory_as<std::uint64_t>(virt_addr(fp)),false);

        auto saved=fp;

        for(unsigned pair=0;pair<5;++pair)if(encoding&(1U<<pair)){
            saved-=16;

            for(unsigned j=0;j<2;++j)out.write(register_info_by_name("x"+std::to_string(19+pair*2+j)),proc.read_memory_as<std::uint64_t>(virt_addr(saved+j*8)),false);
        }

        for(unsigned pair=0;pair<4;++pair)if(encoding&(1U<<(pair+8))){
            saved-=16;

            for(unsigned j=0;j<2;++j){byte128 v{};auto raw=proc.read_memory(virt_addr(saved+j*8),8);std::copy(raw.begin(),raw.end(),v.begin());out.write(register_info_by_name("v"+std::to_string(8+pair*2+j)),v,false);}
        }
    }else return std::nullopt;
    regs.set_cfa(virt_addr(cfa));out.write_by_id(register_id::sp,cfa,false);out.write_by_id(register_id::pc,std::uint64_t(ret&0x0000ffffffffffffULL),false);return out;
}
