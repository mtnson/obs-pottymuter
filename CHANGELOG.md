# Changelog

## 0.2.1 (2026-11-11)

### Model safety — incompatible models can no longer crash OBS

Whisper models with a truncated ("pruned") ggml vocabulary — several
community Russian models ship this way — crash OBS on the very first
inference. Such models are now detected by their file header
(`n_vocab < 51000`) and are refused everywhere:

- downloaded pruned models are hidden from the model list;
- selecting a not-yet-downloaded pruned model is vetoed at click time:
  an 8-byte HTTP Range request reads the remote file header before any
  transfer happens, and the selection is reverted to the working model
  with an honest error message;
- the download dialog and the model loader refuse pruned models with a
  clear error (full details in the OBS log) instead of crashing.

The currently loaded model is never torn down while a new model
downloads — a failed or refused download keeps the previous model
working and reverts the dropdown, so the filter stays live.

### Honest downloads

- Downloaded files are validated: plausible size and SHA-256 (when the
  catalog provides a checksum). A broken or partial file is never
  reported as success.
- Duplicate download requests (an OBS double-update quirk) are
  deduplicated.
- The download error dialog shows any error text in full (fixed layout
  with scrolling for long reasons) and reports HTTP/curl codes.

### Matching quality

- UTF-8 aware case-insensitive matching before the regex: Cyrillic and
  Latin case folding plus `ё`/`е` normalization — word lists match in
  any register ("БЛЯТЬ", "СуКа", "блять").

### Misc

- Model list UX: downloaded models float to the top with a ✓ check mark
  and their file size; the stored setting remains the plain model name,
  so saved scenes keep working.
- whisper.cpp updated to 0.0.14.
