    dwarf_=std::make_unique<dwarf>(*this);
sdb::elf::~elf()=default;
    auto b=read_file(path);auto magic=record<std::uint32_t>(b,0);
        auto h=record<fat_header>(b,0);bool found=false;
        if(!found)error::send("Universal binary has no ARM64 slice");
    auto h=record<mach_header_64>(b,0);
    bounds(b,sizeof h,h.sizeofcmds);

    auto base=storage_.size();storage_.insert(storage_.end(),b.begin(),b.end());

    std::optional<symtab_command> symtab;
        auto cmd=record<load_command>(b,pos);if(cmd.cmdsize<sizeof cmd||pos+cmd.cmdsize>sizeof(h)+h.sizeofcmds)error::send("Invalid Mach-O load command");
            auto seg=record<segment_command_64>(b,pos);
            if(sizeof(seg)+std::uint64_t(seg.nsects)*sizeof(section_64)>cmd.cmdsize)error::send("Invalid Mach-O section count");
                std::string name(sec.sectname,strnlen(sec.sectname,16));
                if(name.rfind("__",0)==0)name="."+name.substr(2);
