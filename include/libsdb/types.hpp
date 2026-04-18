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
			addr_ += offset;
			return *this;
		}
		virt_addr& operator-=(std::int64_t offset) {
			addr_ -= offset;
			return *this;
		}
		bool operator==(const virt_addr& other) const {
			return addr_ == other.addr_;
		}
		bool operator!=(const virt_addr& other) const {
			return addr_ != other.addr_;
		}
		bool operator<(const virt_addr& other) const {
			return addr_ < other.addr_;
		}
		bool operator<=(const virt_addr& other) const {
			return addr_ <= other.addr_;
		}
		bool operator>(const virt_addr& other) const {
			return addr_ > other.addr_;
		}
		bool operator>=(const virt_addr& other) const {
			return addr_ >= other.addr_;
		}

		file_addr to_file_addr(const elf& obj) const;
		file_addr to_file_addr(const elf_collection& elves) const;
	private:
		std::uint64_t addr_ = 0;
	};

	class file_addr {
	public:
		file_addr() = default;
		file_addr(const elf& obj, std::uint64_t addr)
			: elf_(&obj), addr_(addr) {}

		std::uint64_t addr() const {
			return addr_;
		}
		const elf* elf_file() const {
			return elf_;
		}

		file_addr operator+(std::int64_t offset) const {
			return file_addr(*elf_, addr_ + offset);
		}
		file_addr operator-(std::int64_t offset) const {
			return file_addr(*elf_, addr_ - offset);
		}
		file_addr& operator+=(std::int64_t offset) {
			addr_ += offset;
			return *this;
		}
		file_addr& operator-=(std::int64_t offset) {
			addr_ -= offset;
			return *this;
		}
		bool operator==(const file_addr& other) const {
			return addr_ == other.addr_ and elf_ == other.elf_;
		}
		bool operator!=(const file_addr& other) const {
			return addr_ != other.addr_ or elf_ != other.elf_;
		}
		bool operator<(const file_addr& other) const {
