#include "vocab-probe.h"

#include "ggml-header.h"
#include "plugin-support.h"

#include <obs-module.h>

#include <curl/curl.h>

#include <cstdint>
#include <string>

// Aborts the probe transfer once 8 header bytes are buffered. A non-zero
// request to curl cancels the transfer (CURLE_WRITE_ERROR); the caller
// only looks at the buffer contents, so the cancellation is expected.
static size_t probe_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	auto *buffer = (std::string *)userdata;
	const size_t bytes = size * nmemb;
	if (buffer->size() >= 8)
		return 0;
	if (buffer->size() + bytes > 8)
		buffer->append(ptr, 8 - buffer->size());
	else
		buffer->append(ptr, bytes);
	return buffer->size() >= 8 ? 0 : bytes;
}

ProbeStatus probe_ggml_vocabulary(const std::string &url, int64_t *n_vocab_out,
				  long connect_timeout_seconds)
{
	std::string buffer;

	CURL *curl = curl_easy_init();
	if (curl == nullptr)
		return ProbeStatus::Unknown;
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, probe_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
	// No progress callbacks: the probe is silent.
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	// Exactly the 8 header bytes; servers without Range support answer
	// with the full response, in which case the write callback aborts
	// the transfer after the first chunk anyway.
	curl_easy_setopt(curl, CURLOPT_RANGE, "0-7");
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout_seconds);

	CURLcode res = curl_easy_perform(curl);

	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_easy_cleanup(curl);

	if (buffer.size() < 8) {
		obs_log(LOG_WARNING,
			"Header probe of '%s' failed (curl=%d, HTTP %ld, %zu bytes); download continues as usual",
			url.c_str(), (int)res, http_code, buffer.size());
		return ProbeStatus::Unknown;
	}

	const uint32_t magic =
		(uint32_t)(uint8_t)buffer[0] | ((uint32_t)(uint8_t)buffer[1] << 8) |
		((uint32_t)(uint8_t)buffer[2] << 16) | ((uint32_t)(uint8_t)buffer[3] << 24);
	const uint32_t n_vocab =
		(uint32_t)(uint8_t)buffer[4] | ((uint32_t)(uint8_t)buffer[5] << 8) |
		((uint32_t)(uint8_t)buffer[6] << 16) | ((uint32_t)(uint8_t)buffer[7] << 24);

	if (magic != 0x67676d6cu) { // "ggml"
		obs_log(LOG_WARNING,
			"Header probe of '%s' found magic 0x%08X, not a whisper ggml header; download continues as usual",
			url.c_str(), magic);
		return ProbeStatus::Unknown;
	}

	obs_log(LOG_INFO, "Header probe of '%s': n_vocab=%u", url.c_str(), n_vocab);
	*n_vocab_out = (int64_t)n_vocab;
	return is_whisper_vocab_compatible((int64_t)n_vocab) ? ProbeStatus::Compatible
							     : ProbeStatus::Pruned;
}
