#ifndef SDB_DISASSEMBLER_HPP
#define SDB_DISASSEMBLER_HPP

#include <libsdb/process.hpp>
#include <optional>

namespace sdb {
    class disassembler {
        struct instruction {
            virt_addr address;
