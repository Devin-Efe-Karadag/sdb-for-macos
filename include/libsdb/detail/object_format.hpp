#pragma once
#include <cstdint>
// Normalized section/symbol records retain the book's interface. These are
// internal records, never read as ELF structures from a Mach-O file.
struct Elf64_Ehdr { std::uint64_t e_entry=0; };
