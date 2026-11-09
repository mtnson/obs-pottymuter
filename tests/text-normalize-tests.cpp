// Tests for text and regex normalization.
//
// Deliberately free of profanity: stop-word lists are not stored in the
// repository (decision 2026-11-09). Only the two transforms are covered -
// code point case folding and yo -> e - plus the guarantee that everything
// else (punctuation, spacing, repeats, masks, invalid bytes) is untouched,
// so patterns written for other languages keep working as before.

#include "text-normalize.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void expect(const char *name, const std::string &actual, const std::string &expected)
{
	if (actual == expected)
		return;
	++failures;
	std::printf("FAIL %-30s got '%s' expected '%s'\n", name, actual.c_str(),
		    expected.c_str());
}

void test_text()
{
	using pottymuter::normalize_text;

	expect("empty", normalize_text(""), "");
	expect("ascii lowercase", normalize_text("Hello"), "hello");
	expect("cyrillic lowercase", normalize_text("ПРИВЕТ"), "привет");
	expect("yo to e", normalize_text("Ёжик"), "ежик");
	expect("yo uppercase", normalize_text("ЁЛКА"), "елка");
	expect("yo mixed", normalize_text("звёзды"), "звезды");
	expect("ukrainian", normalize_text("ІЇЄ"), "іїє");
	expect("latin accents", normalize_text("ÀÉÎ"), "àéî");
	expect("digits kept", normalize_text("123ABC"), "123abc");

	// Anything below must stay byte-identical: patterns for other languages
	// rely on punctuation, spacing and repeats.
	expect("punctuation kept", normalize_text("ПРИВЕТ, МИР!"), "привет, мир!");
	expect("dots kept", normalize_text("Привет...мир"), "привет...мир");
	expect("spaces kept", normalize_text("  Два   Слова  "), "  два   слова  ");
	expect("repeats kept", normalize_text("Привеееет"), "привеееет");
	expect("newlines kept", normalize_text("Строка\nВторая"), "строка\nвторая");
	expect("star kept", normalize_text("Зв**зда"), "зв**зда");
	expect("url-ish kept", normalize_text("HTTPS://EXAMPLE.COM"), "https://example.com");

	std::string broken = "АБ";
	broken.push_back(static_cast<char>(0xFF));
	std::string expected_broken = "аб";
	expected_broken.push_back(static_cast<char>(0xFF));
	expect("invalid byte kept", normalize_text(broken), expected_broken);
}

void test_pattern()
{
	using pottymuter::normalize_pattern;

	expect("pattern lower", normalize_pattern("ПРИВЕТ|ЁЖ"), "привет|еж");
	expect("pattern class", normalize_pattern("[А-Я]+"), "[а-я]+");
	expect("pattern escapes kept", normalize_pattern("\\W+ПРИВЕТ"), "\\W+привет");
	expect("pattern quantifier kept", normalize_pattern("А+Б*"), "а+б*");
	expect("pattern groups kept", normalize_pattern("(?:А|Б)"), "(?:а|б)");
	expect("pattern mask kept", normalize_pattern("Зв**зда"), "зв**зда");
	expect("pattern escaped dot kept", normalize_pattern("A\\.B"), "a\\.b");
	expect("pattern punctuation kept", normalize_pattern("a|b|c"), "a|b|c");
}

} // namespace

int main()
{
	test_text();
	test_pattern();

	if (failures == 0) {
		std::printf("OK: all normalization tests passed\n");
		return 0;
	}

	std::printf("FAILED: %d test(s)\n", failures);
	return 1;
}
