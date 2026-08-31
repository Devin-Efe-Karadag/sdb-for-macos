#pragma once
#include <cstdint>
// Normalized section/symbol records retain the book's interface. These are
// internal records, never read as ELF structures from a Mach-O file.
struct Elf64_Ehdr { std::uint64_t e_entry=0; };
struct Elf64_Shdr { std::uint32_t sh_name=0; std::uint64_t sh_addr=0,sh_offset=0,sh_size=0,sh_entsize=0; };
struct Elf64_Sym { std::uint32_t st_name=0; std::uint8_t st_info=0; std::uint64_t st_value=0,st_size=0; };
constexpr int STT_FUNC=2,STT_TLS=6;
constexpr int ELF64_ST_TYPE(unsigned char type){return type&15;}
