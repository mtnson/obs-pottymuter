#ifndef VOCAB_PROBE_H
#define VOCAB_PROBE_H

#include <cstdint>
#include <string>

// Result of a pre-download header probe.
enum class ProbeStatus {
	// Header says ggml and n_vocab is standard: safe to download.
	Compatible,
	// Header says ggml and n_vocab is truncated (pruned): refuse.
	Pruned,
	// Not enough information (transfer failure, no Range support,
	// missing magic). The caller decides: downloads proceed, the local
	// header check before loading keeps the honest refusal in any case.
	Unknown,
};

// Fetch the first 8 bytes of the remote model file (HTTP Range request)
// and read the whisper ggml header from them: magic "ggml", then n_vocab.
// This catches pruned-vocabulary models (n_vocab < 51000) BEFORE hundreds
// of megabytes are downloaded - they would crash OBS on the first
// inference. Verified positively only: magic/n_vocab failures stay
// Unknown and never block a download. When called on the UI thread
// (selection veto path) the connection timeout bounds the possible freeze.
ProbeStatus probe_ggml_vocabulary(const std::string &url, int64_t *n_vocab_out,
				  long connect_timeout_seconds = 10);

#endif // VOCAB_PROBE_H
