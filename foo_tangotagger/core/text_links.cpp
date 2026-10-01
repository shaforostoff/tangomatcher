#include "text_links.h"

#include <algorithm>
#include <cstring>

namespace tangotagger
{
	namespace
	{
		char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

		bool starts_with_ci(const std::string & text, std::size_t at, const char * prefix)
		{
			const std::size_t n = std::strlen(prefix);
			if (text.size() - at < n) return false;
			for (std::size_t i = 0; i < n; i++)
				if (lower(text[at + i]) != prefix[i]) return false;
			return true;
		}

		//! ASCII only: an address may follow an opening typographic quote.
		bool is_word_char(char c)
		{
			return (c >= '0' && c <= '9') || (lower(c) >= 'a' && lower(c) <= 'z');
		}

		//! Whether a byte can be part of an address.
		bool in_address(char c)
		{
			const unsigned char u = static_cast<unsigned char>(c);
			if (u <= 0x20 || u >= 0x7F) return false;
			return c != '"' && c != '<' && c != '>' && c != '`';
		}

		//! The length of the address prefix at `at`, or 0 for none.
		std::size_t prefix_at(const std::string & text, std::size_t at)
		{
			// Not in the middle of a word: "xhttp://" is not an address.
			if (at > 0 && (is_word_char(text[at - 1]) || text[at - 1] == '.' || text[at - 1] == '/'))
				return 0;
			if (starts_with_ci(text, at, "https://")) return 8;
			if (starts_with_ci(text, at, "http://")) return 7;
			if (starts_with_ci(text, at, "www.")) return 4;
			return 0;
		}
	}

	std::vector<text_link> find_links(const std::string & text)
	{
		std::vector<text_link> out;
		std::size_t i = 0;
		while (i < text.size())
		{
			const std::size_t prefix = prefix_at(text, i);
			if (prefix == 0) { i++; continue; }

			std::size_t end = i + prefix;
			while (end < text.size() && in_address(text[end])) end++;

			// Sentence punctuation after the address, and closing brackets it
			// did not open.
			while (end > i + prefix)
			{
				const char c = text[end - 1];
				if (std::strchr(".,;:!?'", c) != nullptr) { end--; continue; }
				if (c == ')' || c == ']')
				{
					const char open = c == ')' ? '(' : '[';
					const auto first = text.begin() + static_cast<std::ptrdiff_t>(i);
					const auto last = text.begin() + static_cast<std::ptrdiff_t>(end);
					if (std::count(first, last, open) < std::count(first, last, c)) { end--; continue; }
				}
				break;
			}

			// Something after the prefix, with a dot in the host of a bare
			// "www." - "www." alone, or "http://" alone, is not an address.
			const std::string address = text.substr(i, end - i);
			const bool bare = prefix == 4;
			if (end > i + prefix && (!bare || address.find('.', prefix) != std::string::npos))
			{
				text_link link;
				link.begin = i;
				link.end = end;
				link.url = bare ? "http://" + address : address;
				out.push_back(std::move(link));
				i = end;
			}
			else i += prefix;
		}
		return out;
	}
}
