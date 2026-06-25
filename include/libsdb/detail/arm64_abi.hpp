#pragma once
#include <libsdb/type.hpp>
namespace sdb {
// AAPCS64 homogeneous floating-point aggregates use up to four SIMD registers.
struct arm64_hfa {std::size_t element_size=0;std::vector<std::size_t> offsets;};
std::optional<arm64_hfa> classify_arm64_hfa(type value);
}
