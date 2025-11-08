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
