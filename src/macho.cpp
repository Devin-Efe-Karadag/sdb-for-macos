#include <libsdb/elf.hpp>
#include <libsdb/dwarf.hpp>
#include <libsdb/error.hpp>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/fat.h>
#include <libkern/OSByteOrder.h>
#include <fstream>
#include <cxxabi.h>
#include <cstring>
#include <limits>
namespace {
std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary|std::ios::ate);

    if(!in) sdb::error::send("Cannot open Mach-O file: "+path.string());

    auto n=in.tellg();if(n<0 || n>1024LL*1024*1024)sdb::error::send("Invalid Mach-O file size");

    std::vector<std::byte> b(static_cast<std::size_t>(n));in.seekg(0);

    if(!in.read(reinterpret_cast<char*>(b.data()),n))sdb::error::send("Cannot read Mach-O file");return b;
}
template<class T>T record(const std::vector<std::byte>& b,std::size_t off){
    if(off>b.size()||sizeof(T)>b.size()-off)sdb::error::send("Truncated Mach-O record");
    T t;memcpy(&t,b.data()+off,sizeof t);return t;
}
void bounds(const std::vector<std::byte>& b,std::uint64_t off,std::uint64_t size){if(off>b.size()||size>b.size()-off)sdb::error::send("Mach-O range outside file");}
}
sdb::elf::elf(const std::filesystem::path& path):path_(path){
    parse_macho(path,false);

    auto dsym=std::filesystem::path(path.string()+".dSYM")/"Contents/Resources/DWARF"/path.filename();

    if(std::filesystem::exists(dsym))parse_macho(dsym,true);
    data_=storage_.data();file_size_=storage_.size();build_section_map();build_symbol_maps();
    dwarf_=std::make_unique<dwarf>(*this);
}
sdb::elf::~elf()=default;
void sdb::elf::parse_macho(const std::filesystem::path& path,bool debug_only){
    auto b=read_file(path);auto magic=record<std::uint32_t>(b,0);

    if(magic==FAT_CIGAM){
        auto h=record<fat_header>(b,0);bool found=false;

        for(unsigned i=0;i<OSSwapBigToHostInt32(h.nfat_arch);++i){auto a=record<fat_arch>(b,sizeof(h)+i*sizeof(fat_arch));if(OSSwapBigToHostInt32(a.cputype)==CPU_TYPE_ARM64){auto off=OSSwapBigToHostInt32(a.offset),size=OSSwapBigToHostInt32(a.size);bounds(b,off,size);b=std::vector<std::byte>(b.begin()+off,b.begin()+off+size);found=true;break;}}

        if(!found)error::send("Universal binary has no ARM64 slice");
    }

    auto h=record<mach_header_64>(b,0);

    if(h.magic!=MH_MAGIC_64||h.cputype!=CPU_TYPE_ARM64)error::send("Expected a little-endian ARM64 Mach-O image");
    bounds(b,sizeof h,h.sizeofcmds);

    auto base=storage_.size();storage_.insert(storage_.end(),b.begin(),b.end());

    std::optional<symtab_command> symtab;

    std::uint64_t entryoff=0;std::size_t pos=sizeof(h);

    for(unsigned i=0;i<h.ncmds;++i){
        auto cmd=record<load_command>(b,pos);if(cmd.cmdsize<sizeof cmd||pos+cmd.cmdsize>sizeof(h)+h.sizeofcmds)error::send("Invalid Mach-O load command");

        if(cmd.cmd==LC_SEGMENT_64){
            auto seg=record<segment_command_64>(b,pos);

            if(!debug_only&&std::string_view(seg.segname,strnlen(seg.segname,16))=="__TEXT")preferred_base_=seg.vmaddr;

            if(sizeof(seg)+std::uint64_t(seg.nsects)*sizeof(section_64)>cmd.cmdsize)error::send("Invalid Mach-O section count");

            for(unsigned j=0;j<seg.nsects;++j){auto sec=record<section_64>(b,pos+sizeof seg+j*sizeof(section_64));
                std::string name(sec.sectname,strnlen(sec.sectname,16));

                if(debug_only&&name.rfind("__debug_",0)!=0)continue;

                if(name.rfind("__",0)==0)name="."+name.substr(2);

                if(name==".debug_str_offs")name=".debug_str_offsets";

                if((sec.flags&SECTION_TYPE)!=S_ZEROFILL&&(sec.flags&SECTION_TYPE)!=S_THREAD_LOCAL_ZEROFILL)bounds(b,sec.offset,sec.size);
                else sec.offset=0;
                section_names_.push_back(name);
                section_headers_.push_back({static_cast<std::uint32_t>(section_names_.size()-1),sec.addr,base+sec.offset,sec.size,static_cast<std::uint64_t>((sec.flags&SECTION_TYPE)==S_ZEROFILL||(sec.flags&SECTION_TYPE)==S_THREAD_LOCAL_ZEROFILL)});
            }
        }else if(cmd.cmd==LC_SYMTAB&&!debug_only)symtab=record<symtab_command>(b,pos);
        else if(cmd.cmd==LC_MAIN&&!debug_only)entryoff=record<entry_point_command>(b,pos).entryoff;
        pos+=cmd.cmdsize;
    }

    if(!debug_only)header_.e_entry=preferred_base_+entryoff;

    if(symtab){bounds(b,symtab->symoff,std::uint64_t(symtab->nsyms)*sizeof(nlist_64));bounds(b,symtab->stroff,symtab->strsize);
        for(unsigned i=0;i<symtab->nsyms;++i){auto sym=record<nlist_64>(b,symtab->symoff+i*sizeof(nlist_64));if((sym.n_type&N_STAB)||(sym.n_type&N_TYPE)!=N_SECT||!sym.n_value)continue;
            if(sym.n_un.n_strx>=symtab->strsize)error::send("Invalid Mach-O symbol name");

            const char* name=reinterpret_cast<const char*>(b.data()+symtab->stroff+sym.n_un.n_strx);auto max=symtab->strsize-sym.n_un.n_strx;auto len=strnlen(name,max);if(len==max)error::send("Unterminated Mach-O symbol name");

            if(len&&*name=='_'){++name;--len;}symbol_names_.emplace_back(name,len);

            bool function=sym.n_sect&&sym.n_sect<=section_headers_.size()&&get_section_name(section_headers_[sym.n_sect-1].sh_name)==".text";
            symbol_table_.push_back({static_cast<std::uint32_t>(symbol_names_.size()-1),static_cast<std::uint8_t>(function?STT_FUNC:1),sym.n_value,0});
        }

        std::sort(symbol_table_.begin(),symbol_table_.end(),[](auto& a,auto& b){return a.st_value<b.st_value;});

        for(std::size_t i=0;i<symbol_table_.size();++i){auto& s=symbol_table_[i];auto section=get_section_containing_address(file_addr(*this,s.st_value));auto end=section?section->sh_addr+section->sh_size:s.st_value+1;if(i+1<symbol_table_.size()&&symbol_table_[i+1].st_value>s.st_value)end=std::min(end,symbol_table_[i+1].st_value);s.st_size=end-s.st_value;}
    }
}
std::string_view sdb::elf::get_section_name(std::size_t i)const{return section_names_.at(i);}
std::string_view sdb::elf::get_string(std::size_t i)const{return symbol_names_.at(i);}
void sdb::elf::build_section_map(){for(auto& s:section_headers_)section_map_[get_section_name(s.sh_name)]=&s;}
void sdb::elf::build_symbol_maps(){for(auto& s:symbol_table_){auto name=std::string(get_string(s.st_name));symbol_name_map_.emplace(name,&s);int status;char* demangled=abi::__cxa_demangle(name.c_str(),nullptr,nullptr,&status);if(!status){symbol_name_map_.emplace(demangled,&s);free(demangled);}symbol_addr_map_.emplace(std::make_pair(file_addr(*this,s.st_value),file_addr(*this,s.st_value+s.st_size)),&s);}}
std::uint64_t sdb::elf::offset_to_address(std::uint64_t off)const{for(auto& s:section_headers_)if(off>=s.sh_offset&&off-s.sh_offset<s.sh_size)return s.sh_addr+off-s.sh_offset;error::send("Offset has no virtual address");}
std::optional<const Elf64_Shdr*>
sdb::elf::get_section(std::string_view name) const {
	if (section_map_.count(name) == 0) {
		return std::nullopt;
	}
	return section_map_.at(name);
}

sdb::span<const std::byte>
sdb::elf::get_section_contents(std::string_view name) const {
	if (auto sect = get_section(name); sect) {
		if(sect.value()->sh_entsize==1) return {nullptr,std::size_t(0)};
        return { data_ + sect.value()->sh_offset, sect.value()->sh_size };
	}
	return { nullptr, std::size_t(0) };
}

const Elf64_Shdr* sdb::elf::get_section_containing_address(
	file_addr addr) const {
	if (addr.elf_file() != this) return nullptr;
	for (auto& section : section_headers_) {
		if (section.sh_addr <= addr.addr() and
			section.sh_addr + section.sh_size > addr.addr()) {
			return &section;
		}
	}
	return nullptr;
}

const Elf64_Shdr* sdb::elf::get_section_containing_address(virt_addr addr) const {
	for (auto& section : section_headers_) {
		if (load_bias_ + section.sh_addr <= addr and
			load_bias_ + section.sh_addr + section.sh_size > addr) {
			return &section;
		}
	}
	return nullptr;
}

std::optional<sdb::file_addr> sdb::elf::get_section_start_address(
	std::string_view name) const {
	if (auto sect = get_section(name); sect) {
		return file_addr{ *this, sect.value()->sh_addr };
	}
	return std::nullopt;
}

std::vector<const Elf64_Sym*>
sdb::elf::get_symbols_by_name(std::string_view name) const {
	auto [begin, end] = symbol_name_map_.equal_range(std::string(name));

	std::vector<const Elf64_Sym*> ret;
	std::transform(begin, end, std::back_inserter(ret),
		[](auto& pair) { return pair.second; });
	return ret;
}

std::optional<const Elf64_Sym*>
sdb::elf::get_symbol_at_address(file_addr address) const {
	if (address.elf_file() != this) return std::nullopt;
	file_addr null_addr;
	auto it = symbol_addr_map_.find({ address, null_addr });
	if (it == end(symbol_addr_map_)) return std::nullopt;

	return it->second;
}

std::optional<const Elf64_Sym*>
sdb::elf::get_symbol_at_address(virt_addr address) const {
	return get_symbol_at_address(address.to_file_addr(*this));
}

std::optional<const Elf64_Sym*>
sdb::elf::get_symbol_containing_address(file_addr address) const {
	if (address.elf_file() != this or symbol_addr_map_.empty())
		return std::nullopt;

	file_addr null_addr;
	auto it = symbol_addr_map_.lower_bound({ address, null_addr });

	if (it != end(symbol_addr_map_)) {
		if (auto [key, value] = *it; key.first == address) {
			return value;
		}
	}

	if (it == begin(symbol_addr_map_)) return std::nullopt;

	--it;
	if (auto [key, value] = *it;
		key.first < address and key.second > address) {
		return value;
	}

	return std::nullopt;
}

std::optional<const Elf64_Sym*>
sdb::elf::get_symbol_containing_address(virt_addr address) const {
	return get_symbol_containing_address(address.to_file_addr(*this));
}

const sdb::elf* sdb::elf_collection::get_elf_containing_address(
	virt_addr address) const {
	for (auto& elf : elves_) {
		if (auto section = elf->get_section_containing_address(address); section) {
			return elf.get();
		}
	}
	return nullptr;
}

const sdb::elf* sdb::elf_collection::get_elf_by_path(
	std::filesystem::path path) const {
	for (auto& elf : elves_) {
		if (elf->path() == path) {
			return elf.get();
		}
	}
	return nullptr;
}

const sdb::elf* sdb::elf_collection::get_elf_by_filename(
	std::string_view name) const {
	for (auto& elf : elves_) {
		if (elf->path().filename() == name) {
			return elf.get();
		}
	}
	return nullptr;
}
