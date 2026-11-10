// Tests for read_ggml_model_n_vocab(). No OBS dependencies: the reader is
// exercised on synthetic files with hand-written headers, including the
// pruned vocabulary size that crashed OBS (4833) and the standard sizes.

#include "ggml-header.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int failures = 0;

void expect(const char *name, int64_t actual, int64_t expected)
{
	if (actual == expected)
		return;
	++failures;
	std::printf("FAIL %-30s got %lld expected %lld\n", name,
		    (long long)actual, (long long)expected);
}

// Write a synthetic model header and read n_vocab back through the reader.
// total_size limits the file length (4 = only the magic bytes: truncated).
int64_t header_case(std::uint32_t magic, std::uint32_t n_vocab, std::size_t total_size)
{
	const std::filesystem::path path = std::filesystem::temp_directory_path() /
					   ("pottymuter-ggml-header-test-" + std::to_string(magic) +
					    "-" + std::to_string(n_vocab) + "-" +
					    std::to_string(total_size));
	{
		std::string bytes;
		const auto push_u32 = [&bytes](std::uint32_t value) {
			for (std::size_t i = 0; i < sizeof(value); ++i)
				bytes.push_back((char)((value >> (8 * i)) & 0xffu));
		};
		push_u32(magic);
		push_u32(n_vocab);
		bytes.resize(total_size < bytes.size() ? total_size : bytes.size());
		std::ofstream file(path, std::ios::binary);
		file.write(bytes.data(), (std::streamsize)bytes.size());
	}
	const int64_t result = read_ggml_model_n_vocab(path);
	std::filesystem::remove(path);
	return result;
}

void test_headers()
{
	// 0x67676d6c == "ggml" (little endian disk bytes: 'g' 'g' 'm' 'l').
	constexpr std::uint32_t kGgmlMagic = 0x67676d6cu;
	constexpr std::uint32_t kBadMagic = 0x12345678u;

	expect("pruned vocab (observed crash)", header_case(kGgmlMagic, 4833, 8), 4833);
	expect("en-only standard", header_case(kGgmlMagic, 51864, 8), 51864);
	expect("multilingual standard", header_case(kGgmlMagic, 51865, 8), 51865);
	expect("large-v3 standard", header_case(kGgmlMagic, 51866, 8), 51866);

	// Non-whisper ggml files (bad magic) are reported as unknown (-1).
	expect("bad magic", header_case(kBadMagic, 51865, 8), -1);
	// Zero or absurd n_vocab values are treated as unknown too.
	expect("zero vocab", header_case(kGgmlMagic, 0, 8), -1);
	expect("absurd vocab", header_case(kGgmlMagic, 2000001, 8), -1);
	// Truncated header is unknown.
	expect("truncated header", header_case(kGgmlMagic, 51865, 4), -1);

	// Missing file is unknown.
	expect("missing file",
	       read_ggml_model_n_vocab(std::filesystem::temp_directory_path() /
				       "pottymuter-ggml-header-test-missing"),
	       -1);
}

void test_compatibility()
{
	// Pruned sizes are incompatible; unknown (-1) is allowed so that
	// only positively identified pruned models are filtered out.
	if (is_whisper_vocab_compatible(2911)) {
		++failures;
		std::printf("FAIL %-30s\n", "pruned 2911 must be incompatible");
	}
	if (is_whisper_vocab_compatible(4833)) {
		++failures;
		std::printf("FAIL %-30s\n", "pruned 4833 must be incompatible");
	}
	if (is_whisper_vocab_compatible(50999)) {
		++failures;
		std::printf("FAIL %-30s\n", "just below threshold must be incompatible");
	}
	if (!is_whisper_vocab_compatible(51864) || !is_whisper_vocab_compatible(51865) ||
	    !is_whisper_vocab_compatible(51866)) {
		++failures;
		std::printf("FAIL %-30s\n", "standard vocab sizes must be compatible");
	}
	if (!is_whisper_vocab_compatible(52000)) {
		++failures;
		std::printf("FAIL %-30s\n", "larger vocab must be compatible");
	}
	if (!is_whisper_vocab_compatible(-1)) {
		++failures;
		std::printf("FAIL %-30s\n", "unknown must be compatible");
	}
}

} // namespace

int main()
{
	test_headers();
	test_compatibility();
	if (failures != 0) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("ggml-header tests: all passed\n");
	return 0;
}
