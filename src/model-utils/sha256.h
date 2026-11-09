#pragma once

/*
 * Minimal SHA-256 file hashing for model validation.
 * FIPS 180-4, no external dependencies (the vendored deps of this plugin do
 * not ship a crypto library we can link against).
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stream the file at `path` through SHA-256 and return the digest as a
 * lowercase hex string.
 *
 * Returns an empty string when the file cannot be read, or when the
 * built-in self-test fails (checked once on first use).
 */
extern const char *sha256_file_hex(const char *path);

#ifdef __cplusplus
}
#endif
