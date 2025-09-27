#ifndef SDB_TYPES_HPP
#define SDB_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <cassert>

namespace sdb {
	using byte64 = std::array<std::byte, 8>;
	using byte128 = std::array<std::byte, 16>;

	class file_addr;
	class elf;
	class elf_collection;
	class virt_addr {
	public:
		virt_addr() = default;
		explicit virt_addr(std::uint64_t addr)
			: addr_(addr) {}

		std::uint64_t addr() const {
			return addr_;
		}

		virt_addr operator+(std::int64_t offset) const {
			return virt_addr(addr_ + offset);
		}
		virt_addr operator-(std::int64_t offset) const {
			return virt_addr(addr_ - offset);
		}
		virt_addr& operator+=(std::int64_t offset) {
