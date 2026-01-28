#pragma once
#include <libsdb/registers.hpp>
#include <optional>
namespace sdb {
std::optional<registers> unwind_compact(const process&, file_addr, registers&);
}
