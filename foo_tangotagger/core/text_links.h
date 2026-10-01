#pragma once

// Web addresses in lyrics and in the notes around them, for the lyrics panel
// to make clickable.
//
// Standard C++ only, like the rest of core/, so core_test can check it.

#include <cstddef>
#include <string>
#include <vector>

namespace tangotagger
{
	struct text_link
	{
		std::size_t begin = 0;  //!< byte offset of the address in the text
		std::size_t end = 0;    //!< one past its last byte
		std::string url;        //!< what to open: the address, with http:// added to a bare www.
	};

	//! The http://, https:// and www. addresses in a UTF-8 text, in order.
	//!
	//! An address ends at whitespace, at a quote or angle bracket, or at any
	//! non-ASCII character (the links in the data are percent-encoded; a
	//! closing typographic quote is not part of an address). Punctuation that
	//! ends the sentence - "see www.x.com." - is left out, and so is a closing
	//! bracket the address did not open: "(http://x.com)".
	//!
	//! Only web addresses are found, so opening one can never start a program
	//! or open a local file.
	std::vector<text_link> find_links(const std::string & text);
}
