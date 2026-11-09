#include "sha256.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include <obs-module.h>
#include "plugin-support.h"

namespace {

inline uint32_t rotr(uint32_t x, uint32_t n)
{
	return (x >> n) | (x << (32 - n));
}

// FIPS 180-4 round constants
constexpr uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
	0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
	0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
	0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
	0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
	0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
	0xc67178f2,
};

struct Sha256 {
	uint32_t h[8];
	uint64_t bitlen = 0;
	uint8_t buf[64];
	size_t buflen = 0;

	Sha256()
	{
		h[0] = 0x6a09e667;
		h[1] = 0xbb67ae85;
		h[2] = 0x3c6ef372;
		h[3] = 0xa54ff53a;
		h[4] = 0x510e527f;
		h[5] = 0x9b05688c;
		h[6] = 0x1f83d9ab;
		h[7] = 0x5be0cd19;
	}

	void transform(const uint8_t chunk[64])
	{
		uint32_t w[64];
		for (int i = 0; i < 16; i++) {
			w[i] = (uint32_t(chunk[i * 4]) << 24) | (uint32_t(chunk[i * 4 + 1]) << 16) |
			       (uint32_t(chunk[i * 4 + 2]) << 8) | uint32_t(chunk[i * 4 + 3]);
		}
		for (int i = 16; i < 64; i++) {
			const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}

		uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
		uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

		for (int i = 0; i < 64; i++) {
			const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
			const uint32_t ch = (e & f) ^ (~e & g);
			const uint32_t temp1 = hh + S1 + ch + K[i] + w[i];
			const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
			const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
			const uint32_t temp2 = S0 + maj;

			hh = g;
			g = f;
			f = e;
			e = d + temp1;
			d = c;
			c = b;
			b = a;
			a = temp1 + temp2;
		}

		h[0] += a;
		h[1] += b;
		h[2] += c;
		h[3] += d;
		h[4] += e;
		h[5] += f;
		h[6] += g;
		h[7] += hh;
	}

	void update(const uint8_t *data, size_t len)
	{
		// the final block encodes the message length in BITS
		bitlen += uint64_t(len) * 8;

		if (buflen > 0) {
			const size_t take = (len < (64 - buflen)) ? len : (64 - buflen);
			std::memcpy(buf + buflen, data, take);
			buflen += take;
			data += take;
			len -= take;
			if (buflen == 64) {
				transform(buf);
				buflen = 0;
			}
		}

		while (len >= 64) {
			transform(data);
			data += 64;
			len -= 64;
		}

		if (len > 0) {
			std::memcpy(buf, data, len);
			buflen = len;
		}
	}

	void final(uint8_t out[32])
	{
		const uint64_t bits = bitlen;

		// padding: 0x80 then zeros so that (buflen % 64) == 56
		uint8_t pad = 0x80;
		update(&pad, 1);
		const uint8_t zero = 0;
		while (buflen != 56)
			update(&zero, 1);

		// 64-bit big-endian length
		uint8_t lenbytes[8];
		for (int i = 0; i < 8; i++)
			lenbytes[i] = uint8_t(bits >> (56 - 8 * i));
		bitlen = bits;      // keep bookkeeping sane for the transform call
		update(lenbytes, 8);

		for (int i = 0; i < 8; i++) {
			out[i * 4] = uint8_t(h[i] >> 24);
			out[i * 4 + 1] = uint8_t(h[i] >> 16);
			out[i * 4 + 2] = uint8_t(h[i] >> 8);
			out[i * 4 + 3] = uint8_t(h[i]);
		}
	}
};

// Blink-light correctness guarantee: verify FIPS 180-4 test vectors once,
// on the first hashing call. A wrong digest means our implementation is
// broken and every file would look "corrupted" - safer to refuse hashing.
bool sha256_selftest()
{
	struct Vec {
		const char *data;
		size_t len;
		const char *hex;
	};
	const Vec vecs[] = {
		{"", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
		{"abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
		{"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
		 "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
	};

	for (const auto &v : vecs) {
		Sha256 ctx;
		ctx.update(reinterpret_cast<const uint8_t *>(v.data), v.len);
		uint8_t dig[32];
		ctx.final(dig);

		char hex[65];
		for (int i = 0; i < 32; i++)
			snprintf(hex + i * 2, 3, "%02x", dig[i]);

		if (std::string(hex) != v.hex) {
			obs_log(LOG_ERROR,
				"SHA-256 self-test FAILED for test vector '%.8s...': got %.64s, expected %.64s",
				v.data[0] ? v.data : "<empty>", hex, v.hex);
			return false;
		}
	}
	return true;
}

} // namespace

const char *sha256_file_hex(const char *path)
{
	static const bool selftest_ok = sha256_selftest();
	if (!selftest_ok) {
		obs_log(LOG_ERROR, "SHA-256 self-test failed - refusing to hash '%s'", path);
		return "";
	}

	std::ifstream file(path, std::ios::binary);
	if (!file) {
		obs_log(LOG_WARNING, "sha256: cannot open '%s'", path);
		return "";
	}

	Sha256 ctx;
	char buf[65536];
	while (file.read(buf, sizeof(buf)) || file.gcount() > 0) {
		const std::streamsize got = file.gcount();
		if (got <= 0)
			break;
		ctx.update(reinterpret_cast<const uint8_t *>(buf), size_t(got));
	}

	// last update() must not run the final() padding trick with stale state
	// (final() restores bitlen, so this is safe)

	uint8_t dig[32];
	ctx.final(dig);

	static thread_local char hexbuf[65];
	for (int i = 0; i < 32; i++)
		snprintf(hexbuf + i * 2, 3, "%02x", dig[i]);
	hexbuf[64] = 0;
	return hexbuf;
}
