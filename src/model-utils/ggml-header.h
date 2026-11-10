#ifndef GGML_HEADER_H
#define GGML_HEADER_H

#include <cstdint>
#include <filesystem>

// Read n_vocab (the first hparams field) from a whisper ggml model file
// without loading the model.
//
// File layout verified against whisper.cpp 1.8.4 (src/whisper.cpp,
// whisper_model_load): uint32 magic "ggml" (0x67676d6c), then int32 n_vocab.
//
// Returns the n_vocab value, or -1 when the file cannot be read, is shorter
// than the header, or does not start with the ggml magic.
int64_t read_ggml_model_n_vocab(const std::filesystem::path &path);

// whisper.cpp addresses special tokens by hardcoded ids (SOT 50257 ...
// token_beg 50363) and offers forced language tokens built on top of them.
// Standard models have 51864 (English-only) or 51865/51866 (multilingual)
// vocabulary entries; pruned models with truncated dictionaries (e.g. 4833)
// make whisper.cpp index beyond the embedding table and crash with an
// out-of-bounds access (c0000005 in whisper_process_logits, observed on
// 2026-11-09). Vocabulary sizes below this threshold are treated as
// incompatible with this plugin.
constexpr int64_t kMinCompatibleWhisperVocab = 51000;

// A negative n_vocab means "unknown" (file unreadable or not a whisper
// ggml header); unknown is allowed so that only models we can positively
// identify as pruned are filtered out.
inline bool is_whisper_vocab_compatible(int64_t n_vocab)
{
	return n_vocab < 0 || n_vocab >= kMinCompatibleWhisperVocab;
}

#endif // GGML_HEADER_H
