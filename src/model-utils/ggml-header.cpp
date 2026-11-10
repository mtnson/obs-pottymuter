#include "ggml-header.h"

#include <cstdint>
#include <fstream>

int64_t read_ggml_model_n_vocab(const std::filesystem::path &path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open())
		return -1;

	std::uint32_t magic = 0;
	std::uint32_t n_vocab = 0;
	file.read(reinterpret_cast<char *>(&magic), sizeof(magic));
	file.read(reinterpret_cast<char *>(&n_vocab), sizeof(n_vocab));
	if (!file)
		return -1;

	// GGML_FILE_MAGIC from whisper.h: 0x67676d6c == "ggml" in little endian.
	if (magic != 0x67676d6cu)
		return -1;

	// Guard against absurd values from corrupt or non-whisper ggml files
	// (the magic prefix is not unique to whisper models).
	if (n_vocab == 0 || n_vocab > 1000000u)
		return -1;

	return static_cast<int64_t>(n_vocab);
}
