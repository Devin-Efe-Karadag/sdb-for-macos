#ifndef SDB_REGISTERS_HPP
#define SDB_REGISTERS_HPP

#include <libsdb/detail/native_user.hpp>
#include <libsdb/register_info.hpp>
#include <variant>
#include <libsdb/types.hpp>

namespace sdb {
    class process;

    class registers {
