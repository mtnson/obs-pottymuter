#include "text-normalize.h"

#include <cstdint>
#include <vector>

namespace {

// Decoded UTF-8 unit. Invalid bytes are kept verbatim ("raw"), so broken
// input is passed through unchanged instead of being replaced with U+FFFD:
// matching must not alter text it does not understand.
struct Unit {
	uint32_t codepoint;
	bool raw;
};

uint32_t to_lower(uint32_t c)
{
	if (c >= 'A' && c <= 'Z')
		return c + 0x20;
	// Yo/yo -> e: handled before the ranges so the result is a plain "e".
	if (c == 0x0401 || c == 0x0451)
		return 0x0435;
	// U+0410..U+042F (uppercase Cyrillic A..Ya) -> lowercase.
	if (c >= 0x0410 && c <= 0x042F)
		return c + 0x20;
	// Other uppercase Cyrillic letters from U+0400..U+040F (І, Ї, Є, Ђ, ...).
	if (c >= 0x0400 && c <= 0x040F)
		return c + 0x50;
	if (c == 0x0490) // Ґ
		return 0x0491;
	// Uppercase Latin-1 letters (À-Þ, except ×).
	if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7)
		return c + 0x20;
	return c;
}

std::vector<Unit> decode_utf8(const std::string &text)
{
	std::vector<Unit> units;
	units.reserve(text.size());

	size_t i = 0;
	while (i < text.size()) {
		const unsigned char lead = static_cast<unsigned char>(text[i]);
		uint32_t cp = 0;
		size_t length = 0;

		if (lead < 0x80) {
			cp = lead;
			length = 1;
		} else if ((lead & 0xE0) == 0xC0) {
			cp = lead & 0x1F;
			length = 2;
		} else if ((lead & 0xF0) == 0xE0) {
			cp = lead & 0x0F;
			length = 3;
		} else if ((lead & 0xF8) == 0xF0) {
			cp = lead & 0x07;
			length = 4;
		} else {
			units.push_back({lead, true});
			++i;
			continue;
		}

		if (i + length > text.size()) {
			units.push_back({lead, true});
			++i;
			continue;
		}

		bool valid = true;
		for (size_t k = 1; k < length; ++k) {
			const unsigned char next = static_cast<unsigned char>(text[i + k]);
			if ((next & 0xC0) != 0x80) {
				valid = false;
				break;
			}
			cp = (cp << 6) | (next & 0x3F);
		}

		if (!valid) {
			units.push_back({lead, true});
			++i;
			continue;
		}

		units.push_back({cp, false});
		i += length;
	}

	return units;
}

void append_unit(std::string &out, const Unit &unit)
{
	if (unit.raw) {
		out.push_back(static_cast<char>(unit.codepoint));
		return;
	}

	const uint32_t cp = unit.codepoint;
	if (cp < 0x80) {
		out.push_back(static_cast<char>(cp));
	} else if (cp < 0x800) {
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else {
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

} // namespace

namespace pottymuter {

std::string normalize_text(const std::string &text)
{
	std::string out;
	out.reserve(text.size());

	for (const Unit &unit : decode_utf8(text)) {
		if (unit.raw) {
			append_unit(out, unit);
			continue;
		}
		append_unit(out, {to_lower(unit.codepoint), false});
	}

	return out;
}

std::string normalize_pattern(const std::string &pattern)
{
	std::string out;
	out.reserve(pattern.size());

	bool escaped = false;
	for (const Unit &unit : decode_utf8(pattern)) {
		if (unit.raw) {
			append_unit(out, unit);
			escaped = false;
			continue;
		}
		if (escaped) {
			append_unit(out, unit);
			escaped = false;
			continue;
		}
		if (unit.codepoint == '\\') {
			append_unit(out, unit);
			escaped = true;
			continue;
		}
		append_unit(out, {to_lower(unit.codepoint), false});
	}

	return out;
}

} // namespace pottymuter
