//# This file is a part of toml++ and is subject to the the terms of the MIT license.
//# Copyright (c) 2019-2020 Mark Gillard <mark.gillard@outlook.com.au>
//# See https://github.com/marzer/tomlplusplus/blob/master/LICENSE for the full license text.

#pragma once
#include "toml_formatter.h"
#include "toml_table.h"
#include "toml_array.h"
#include "toml_utf8.h"

namespace toml::impl
{
	[[nodiscard]] TOML_API
	toml::string default_formatter_make_key_segment(const toml::string& str) noexcept;

	[[nodiscard]] TOML_API
	size_t default_formatter_inline_columns(const node& node) noexcept;

	[[nodiscard]] TOML_API
	bool default_formatter_forces_multiline(const node& node, size_t starting_column_bias = 0) noexcept;
}


namespace toml
{
	template <typename T, typename U>
	std::basic_ostream<T>& operator << (std::basic_ostream<T>&, default_formatter<U>&);
	template <typename T, typename U>
	std::basic_ostream<T>& operator << (std::basic_ostream<T>&, default_formatter<U>&&);

	/// \brief	A wrapper for printing TOML objects out to a stream as formatted TOML.
	/// 
	/// \remarks You generally don't need to create an instance of this class explicitly; the stream
	/// 		 operators of the TOML node types already print themselves out using this formatter.
	///
	/// \detail \cpp
	/// auto tbl = toml::table{{
	///		{ "description", "This is some TOML, yo." },
	///		{ "fruit", toml::array{ "apple", "orange", "pear" } },
	///		{ "numbers", toml::array{ 1, 2, 3, 4, 5 } },
	///		{ "table", toml::table{{ { "foo", "bar" } }} }
	/// }};
	///
	/// // these two lines are equivalent:
	///	std::cout << toml::default_formatter{ tbl } << std::endl;
	///	std::cout << tbl << std::endl;
	/// 
	/// \ecpp
	/// 
	/// \out
	/// description = "This is some TOML, yo."
	/// fruit = ["apple", "orange", "pear"]
	/// numbers = [1, 2, 3, 4, 5]
	/// 
	/// [table]
	/// foo = "bar"
	/// \eout
	/// 
	/// \tparam	Char	The underlying character type of the output stream. Must be 1 byte in size.
	template <typename Char = char>
	class TOML_API default_formatter final : impl::formatter<Char>
	{
		private:
			using base = impl::formatter<Char>;
			std::vector<toml::string> key_path;

			void print_key_segment(const toml::string& str)
			{
				if (str.empty())
					impl::print_to_stream("''"sv, base::stream());
				else
				{
					bool requiresQuotes = false;
					{
						impl::utf8_decoder decoder;
						for (size_t i = 0; i < str.length() && !requiresQuotes; i++)
						{
							decoder(static_cast<uint8_t>(str[i]));
							if (decoder.error())
								requiresQuotes = true;
							else if (decoder.has_code_point())
								requiresQuotes = !impl::is_bare_key_character(decoder.codepoint);
						}
					}

					if (requiresQuotes)
						base::print_quoted_string(str);
					else
						impl::print_to_stream(str, base::stream());
				}
				base::clear_naked_newline();
			}

			void print_key_path()
			{
				for (const auto& segment : key_path)
				{
					if (std::addressof(segment) > key_path.data())
						impl::print_to_stream('.', base::stream());
					impl::print_to_stream(segment, base::stream());
				}
				base::clear_naked_newline();
			}

			void print_inline(const table& /*tbl*/);

			void print(const array& arr)
			{
				if (arr.empty())
					impl::print_to_stream("[]"sv, base::stream());
				else
				{
					const auto original_indent = base::indent();
					const auto multiline = impl::default_formatter_forces_multiline(
						arr,
						base::indent_columns * static_cast<size_t>(original_indent < 0 ? 0 : original_indent)
					);
					impl::print_to_stream("["sv, base::stream());
					if (multiline)
					{
						if (original_indent < 0)
							base::indent(0);
						base::increase_indent();
					}
					else
						impl::print_to_stream(' ', base::stream());

					for (size_t i = 0; i < arr.size(); i++)
					{
						if (i > 0_sz)
						{
							impl::print_to_stream(',', base::stream());
							if (!multiline)
								impl::print_to_stream(' ', base::stream());
						}

						if (multiline)
						{
							base::print_newline(true);
							base::print_indent();
						}

						auto& v = arr[i];
						const auto type = v.type();
						switch (type)
						{
							case node_type::table: print_inline(*reinterpret_cast<const table*>(&v)); break;
							case node_type::array: print(*reinterpret_cast<const array*>(&v)); break;
							default:
								base::print(v, type);
						}

					}
					if (multiline)
					{
						base::indent(original_indent);
						base::print_newline(true);
						base::print_indent();
					}
					else
						impl::print_to_stream(' ', base::stream());
					impl::print_to_stream("]"sv, base::stream());
				}
				base::clear_naked_newline();
			}

			void print(const table& tbl)
			{
				static constexpr auto is_non_inline_array_of_tables = [](auto&& nde) noexcept
				{
					auto arr = nde.as_array();
					return arr
						&& arr->is_array_of_tables()
						&& !arr->template get_as<table>(0_sz)->is_inline();
				};

				//values, arrays, and inline tables/table arrays
				for (auto [k, v] : tbl)
				{
					const auto type = v.type();
					if ((type == node_type::table && !reinterpret_cast<const table*>(&v)->is_inline())
						|| (type == node_type::array && is_non_inline_array_of_tables(v)))
						continue;

					base::print_newline();
					base::print_indent();
					print_key_segment(k);
					impl::print_to_stream(" = "sv, base::stream());
					switch (type)
					{
						case node_type::table: print_inline(*reinterpret_cast<const table*>(&v)); break;
						case node_type::array: print(*reinterpret_cast<const array*>(&v)); break;
						default:
							base::print(v, type);
					}
				}

				//non-inline tables
				for (auto [k, v] : tbl)
				{
					const auto type = v.type();
					if (type != node_type::table || reinterpret_cast<const table*>(&v)->is_inline())
						continue;
					auto& child_tbl = *reinterpret_cast<const table*>(&v);

					//we can skip indenting and emitting the headers for tables that only contain other tables
					//(so we don't over-nest)
					size_t child_value_count{}; //includes inline tables and non-table arrays
					size_t child_table_count{};
					size_t child_table_array_count{};
					for (auto [child_k, child_v] : child_tbl)
					{
						(void)child_k;
						const auto child_type = child_v.type();
						switch (child_type)
						{
							case node_type::table:
								if (reinterpret_cast<const table*>(&child_v)->is_inline())
									child_value_count++;
								else
									child_table_count++;
								break;

							case node_type::array:
								if (is_non_inline_array_of_tables(child_v))
									child_table_array_count++;
								else
									child_value_count++;
								break;

							default:
								child_value_count++;
						}
					}
					bool skip_self = false;
					if (child_value_count == 0_sz && (child_table_count > 0_sz || child_table_array_count > 0_sz))
						skip_self = true;

					if (!skip_self)
						base::increase_indent();
					key_path.push_back(impl::default_formatter_make_key_segment(k));

					if (!skip_self)
					{
						base::print_newline();
						base::print_newline(true);
						base::print_indent();
						impl::print_to_stream("["sv, base::stream());
						print_key_path();
						impl::print_to_stream("]"sv, base::stream());
						base::print_newline(true);
					}

					print(child_tbl);

					key_path.pop_back();
					if (!skip_self)
						base::decrease_indent();
				}

				//table arrays
				for (auto [k, v] : tbl)
				{
					if (!is_non_inline_array_of_tables(v))
						continue;
					auto& arr = *reinterpret_cast<const array*>(&v);

					base::increase_indent();
					key_path.push_back(impl::default_formatter_make_key_segment(k));

					for (size_t i = 0; i < arr.size(); i++)
					{
						base::print_newline();
						base::print_newline(true);
						base::print_indent();
						impl::print_to_stream("[["sv, base::stream());
						print_key_path();
						impl::print_to_stream("]]"sv, base::stream());
						base::print_newline(true);
						print(*reinterpret_cast<const table*>(&arr[i]));
					}

					key_path.pop_back();
					base::decrease_indent();
				}
			}

			void print()
			{
				switch (auto source_type = base::source().type())
				{
					case node_type::table:
					{
						auto& tbl = *reinterpret_cast<const table*>(&base::source());
						if (tbl.is_inline())
							print_inline(tbl);
						else
						{
							base::decrease_indent(); // so root kvps and tables have the same indent
							print(tbl);
						}
						break;
					}

					case node_type::array:
						print(*reinterpret_cast<const array*>(&base::source()));
						break;

					default:
						base::print(base::source(), source_type);
				}
			}

		public:

			/// \brief	Constructs a default formatter and binds it to a TOML object.
			///
			/// \param 	source	The source TOML object.
			/// \param 	flags 	Format option flags.
			TOML_NODISCARD_CTOR
			explicit default_formatter(const toml::node& source, format_flags flags = {}) noexcept
				: base{ source, flags }
			{}

			template <typename T, typename U>
			friend std::basic_ostream<T>& operator << (std::basic_ostream<T>&, default_formatter<U>&);
			template <typename T, typename U>
			friend std::basic_ostream<T>& operator << (std::basic_ostream<T>&, default_formatter<U>&&);
	};

	#if !TOML_ALL_INLINE
		extern template class TOML_API default_formatter<char>;
	#endif

	default_formatter(const table&) -> default_formatter<char>;
	default_formatter(const array&) -> default_formatter<char>;
	template <typename T> default_formatter(const value<T>&) -> default_formatter<char>;

	template <typename Char>
	inline void default_formatter<Char>::print_inline(const toml::table& tbl)
	{
		if (tbl.empty())
			impl::print_to_stream("{}"sv, base::stream());
		else
		{
			impl::print_to_stream("{ "sv, base::stream());

			bool first = false;
			for (auto [k, v] : tbl)
			{
				if (first)
					impl::print_to_stream(", "sv, base::stream());
				first = true;

				print_key_segment(k);
				impl::print_to_stream(" = "sv, base::stream());

				const auto type = v.type();
				switch (type)
				{
					case node_type::table: print_inline(*reinterpret_cast<const table*>(&v)); break;
					case node_type::array: print(*reinterpret_cast<const array*>(&v)); break;
					default:
						base::print(v, type);
				}
			}

			impl::print_to_stream(" }"sv, base::stream());
		}
		base::clear_naked_newline();
	}

	/// \brief	Prints the bound TOML object out to the stream as formatted TOML.
	template <typename T, typename U>
	TOML_FUNC_EXTERNAL_LINKAGE
	std::basic_ostream<T>& operator << (std::basic_ostream<T>& lhs, default_formatter<U>& rhs)
	{
		rhs.attach(lhs);
		rhs.key_path.clear();
		rhs.print();
		rhs.detach();
		return lhs;
	}

	/// \brief	Prints the bound TOML object out to the stream as formatted TOML (rvalue overload).
	template <typename T, typename U>
	TOML_FUNC_EXTERNAL_LINKAGE
	std::basic_ostream<T>& operator << (std::basic_ostream<T>& lhs, default_formatter<U>&& rhs)
	{
		return lhs << rhs; //as lvalue
	}

	template <typename Char>
	TOML_FUNC_EXTERNAL_LINKAGE
	std::basic_ostream<Char>& operator << (std::basic_ostream<Char>& lhs, const table& rhs)
	{
		return lhs << default_formatter<Char>{ rhs };
	}

	template <typename Char>
	TOML_FUNC_EXTERNAL_LINKAGE
	std::basic_ostream<Char>& operator << (std::basic_ostream<Char>& lhs, const array& rhs)
	{
		return lhs << default_formatter<Char>{ rhs };
	}

	#if !TOML_ALL_INLINE
		extern template TOML_API std::ostream& operator << (std::ostream&, default_formatter<char>&);
		extern template TOML_API std::ostream& operator << (std::ostream&, default_formatter<char>&&);
		extern template TOML_API std::ostream& operator << (std::ostream&, const table&);
		extern template TOML_API std::ostream& operator << (std::ostream&, const array&);
	#endif
}

