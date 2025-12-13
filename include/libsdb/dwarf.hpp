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
		};
		struct register_result {
			std::uint64_t reg_num;
		};
		struct data_result {
			span<const std::byte> data;
		};
		struct literal_result {
			std::uint64_t value;
		};
		struct empty_result {};
		using simple_location = std::variant<
			address_result, register_result,
			data_result, literal_result, empty_result>;

		struct pieces_result {
			struct piece {
				simple_location location;
				std::uint64_t bit_size;
				std::uint64_t offset = 0;
			};
			std::vector<piece> pieces;
		};
		using result = std::variant<simple_location, pieces_result>;

		dwarf_expression(const dwarf& parent, span<const std::byte> expr_data, bool in_frame_info)
			: parent_(&parent), expr_data_(expr_data), in_frame_info_(in_frame_info) {}

		result eval(
			const sdb::process& proc,
			const registers& regs,
			bool push_cfa = false) const;
	private:
		const dwarf* parent_;
		span<const std::byte> expr_data_;
        bool in_frame_info_;
	};

	class location_list {
	public:
		location_list(
			const dwarf& parent, const compile_unit& cu,
			span<const std::byte> expr_data, bool in_frame_info)
			: parent_(&parent), cu_(&cu), expr_data_(expr_data), in_frame_info_(in_frame_info) {}

		dwarf_expression::result eval(
			const sdb::process& proc, const registers& regs) const;
	private:
		const dwarf* parent_;
		const compile_unit* cu_;
		span<const std::byte> expr_data_;
        bool in_frame_info_;
	};

	class type;
	class attr {
	public:
		attr(const compile_unit* cu, std::uint64_t type,
			std::uint64_t form, const std::byte* location) :
			cu_(cu), type_(type), form_(form), location_(location) {}

		std::uint64_t name() const { return type_; }
		std::uint64_t form() const { return form_; }

		file_addr as_address() const;
		std::uint32_t as_section_offset() const;
		span<const std::byte> as_block() const;
		std::uint64_t as_int() const;
		std::string_view as_string() const;
		die as_reference() const;
		range_list as_range_list() const;
		dwarf_expression as_expression(bool in_frame_info) const;
		location_list as_location_list(bool in_frame_info) const;
		dwarf_expression::result as_evaluated_location(
			const sdb::process& proc, const registers& regs, bool in_frame_info) const;
		type as_type() const;

	private:
		const compile_unit* cu_;
		std::uint64_t type_;
		std::uint64_t form_;
		const std::byte* location_;
	};

	class elf;
	struct attr_spec {
		std::uint64_t attr;
		std::uint64_t form;
	};
	struct abbrev {
		std::uint64_t code;
		std::uint64_t tag;
		bool has_children;
		std::vector<attr_spec> attr_specs;
	};

	class dwarf;

	class line_table {
	public:
		struct file {
			std::filesystem::path path;
			std::uint64_t modification_time;
			std::uint64_t file_length;
		};

		line_table(sdb::span<const std::byte> data,
			const compile_unit* cu,
			bool default_is_stmt, std::int8_t line_base,
			std::uint8_t line_range, std::uint8_t opcode_base,
			std::vector<std::filesystem::path> include_directories,
			std::vector<file> file_names)
			: data_(data), cu_(cu)
			, default_is_stmt_(default_is_stmt)
			, line_base_(line_base)
			, line_range_(line_range)
			, opcode_base_(opcode_base)
			, include_directories_(std::move(include_directories))
			, file_names_(std::move(file_names))
		{}

		const compile_unit& cu() const { return *cu_; }
		const std::vector<file>& file_names() const { return file_names_; }

		line_table(const line_table&) = delete;
		line_table& operator=(const line_table&) = delete;

		struct entry;

		class iterator;

		iterator begin() const;
		iterator end() const;

		iterator get_entry_by_address(file_addr address) const;
		std::vector<iterator> get_entries_by_line(
			std::filesystem::path path, std::size_t line) const;

	private:
		sdb::span<const std::byte> data_;
		const compile_unit* cu_;
		bool default_is_stmt_;
		std::int8_t line_base_;
		std::uint8_t line_range_;
		std::uint8_t opcode_base_;
		std::vector<std::filesystem::path> include_directories_;
		mutable std::vector<file> file_names_;
	};

	struct line_table::entry {
		bool operator==(const entry& rhs) const {
			return address == rhs.address and
				file_index == rhs.file_index and
				line == rhs.line and
				column == rhs.column and
				discriminator == rhs.discriminator;
		}

		file_addr address;
		std::uint64_t file_index = 1;
		std::uint64_t line = 1;
		std::uint64_t column = 0;
		bool is_stmt;
		bool basic_block_start = false;
		bool end_sequence = false;
		bool prologue_end = false;
		bool epilogue_begin = false;
		std::uint64_t discriminator = 0;
		file* file_entry = nullptr;
	};

	class line_table::iterator {
