#include <libsdb/types.hpp>
#include <libsdb/elf.hpp>
#include <cassert>

sdb::virt_addr sdb::file_addr::to_virt_addr() const {
    assert(elf_ && "to_virt_addr called on null address");
