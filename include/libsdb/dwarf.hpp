#ifndef SDB_DWARF_HPP
#define SDB_DWARF_HPP

#include <libsdb/detail/dwarf.h>
#include <libsdb/registers.hpp>
#include <libsdb/types.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <memory>
#include <string_view>
#include <optional>
#include <string>
#include <filesystem>

namespace sdb {
	class compile_unit;
	class die;

	class range_list {
	public:
		range_list(
			const compile_unit* cu, span<const std::byte> data,
			file_addr base_address)
			: cu_(cu), data_(data), base_address_(base_address) {}

		struct entry {
			file_addr low;
			file_addr high;

			bool contains(file_addr addr) const {
				return low <= addr and addr < high;
			}
		};

		class iterator;
		iterator begin() const;
		iterator end() const;

		bool contains(file_addr address) const;

	private:
		const compile_unit* cu_;
		span<const std::byte> data_;
		file_addr base_address_;
	};

	class range_list::iterator {
	public:
		using value_type = entry;
		using reference = const entry&;
		using pointer = const entry*;
		using difference_type = std::ptrdiff_t;
		using iterator_category = std::forward_iterator_tag;

		iterator(
			const compile_unit* cu,
			span<const std::byte> data,
			file_addr base_address);

		iterator() = default;
		iterator(const iterator&) = default;
		iterator& operator=(const iterator&) = default;

		const entry& operator*() const { return current_; }
		const entry* operator->() const { return &current_; }

		bool operator==(iterator rhs) const { return pos_ == rhs.pos_; }
		bool operator!=(iterator rhs) const { return pos_ != rhs.pos_; }

		iterator& operator++();
		iterator operator++(int);

	private:
		const compile_unit* cu_ = nullptr;
		span<const std::byte> data_{ nullptr,nullptr };
		file_addr base_address_;
		const std::byte* pos_ = nullptr;
		entry current_;
	};

	class dwarf;
	class dwarf_expression {
	public:
		struct address_result {
			virt_addr address;
