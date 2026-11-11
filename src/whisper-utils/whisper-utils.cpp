#include "whisper-utils.h"
#include "plugin-support.h"
#include "model-utils/ggml-header.h"
#include "model-utils/model-downloader.h"
#include "model-utils/vocab-probe.h"
#include "whisper-processing.h"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QMessageBox>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

void update_whisper_model(struct pottymuter_data *gf, obs_data_t *s)
{
	// update the whisper model path
	std::string new_model_path = obs_data_get_string(s, "whisper_model_path");
	const bool is_external_model = new_model_path.find("!!!external!!!") != std::string::npos;

	if (gf->whisper_model_path.empty() || gf->whisper_model_path != new_model_path ||
	    is_external_model) {

		if (gf->whisper_model_path != new_model_path) {
			// model path changed
			obs_log(gf->log_level, "model path changed from %s to %s",
				gf->whisper_model_path.c_str(), new_model_path.c_str());
		}

		// check if the new model is external file
		if (!is_external_model) {
			// new model is not external file

			// Look up the model and verify it BEFORE stopping the
			// whisper thread: on an incompatibility we want to keep
			// the currently loaded model running.
			if (models_info().count(new_model_path) == 0) {
				obs_log(LOG_WARNING, "Model '%s' does not exist",
					new_model_path.c_str());
				return;
			}

			const ModelInfo &model_info = models_info().at(new_model_path);

			// check if the model exists, if not, download it
			std::string model_file_found = find_model_bin_file(model_info);
			if (!model_file_found.empty()) {
				const int64_t n_vocab =
					read_ggml_model_n_vocab(model_file_found);
				if (!is_whisper_vocab_compatible(n_vocab)) {
					obs_log(
						LOG_ERROR,
						"Model file '%s' has a truncated vocabulary (n_vocab=%lld, expecting >= %lld). Such pruned models are incompatible with whisper.cpp and crash OBS when decoding; not loading it, keeping the currently loaded model.",
						model_file_found.c_str(),
						(long long)n_vocab,
						(long long)kMinCompatibleWhisperVocab);
					return;
				}
			}

			// The currently loaded model is intentionally kept alive
			// while the new one downloads: the filter keeps muting
			// during the transfer, and a failed/refused download
			// leaves the previous model untouched. The previous
			// model is freed only right before the new one starts.

			if (model_file_found == "") {
				obs_log(LOG_WARNING, "Whisper model does not exist");

				// OBS has already committed the clicked model
				// into the settings, and this update runs on
				// the UI thread BEFORE the properties view can
				// re-render. Probe the remote ggml header
				// synchronously (one RTT, at most the connect
				// timeout on a dead network) and refuse pruned
				// models right here: the setting reverts to the
				// still-loaded model, the model_selection_refused
				// flag tells the properties view to re-render,
				// so the dropdown never shows the incompatible
				// model and no download dialog opens at all.
				std::string remote_bin_url;
				for (const ModelFileDownloadInfo &file : model_info.files) {
					if (file.url.size() >= 4 &&
					    file.url.compare(file.url.size() - 4, 4,
							     ".bin") == 0) {
						remote_bin_url = file.url;
						break;
					}
				}
				if (!remote_bin_url.empty()) {
					int64_t remote_n_vocab = -1;
					if (probe_ggml_vocabulary(remote_bin_url,
								  &remote_n_vocab,
								  5) ==
					    ProbeStatus::Pruned) {
						obs_log(
							LOG_ERROR,
							"Model '%s' has a truncated vocabulary (n_vocab=%lld, expecting >= %lld); refusing to select it - the model is incompatible with whisper.cpp and would crash OBS. Reverting the selection to the loaded model '%s'",
							new_model_path.c_str(),
							(long long)remote_n_vocab,
							(long long)kMinCompatibleWhisperVocab,
							gf->whisper_model_path.c_str());
						obs_data_t *current_settings =
							obs_source_get_settings(
								gf->context);
						if (current_settings != nullptr) {
							obs_data_set_string(
								current_settings,
								"whisper_model_path",
								gf->whisper_model_path
									.c_str());
							obs_data_release(
								current_settings);
						}
						gf->model_selection_refused = true;
						QMessageBox::warning(
							(QWidget *)obs_frontend_get_main_window(),
							"Model incompatible",
							QString(
								"Incompatible model: truncated vocabulary (n_vocab=%1). The model stays unloaded, the previous model keeps working.")
								.arg((long long)
									remote_n_vocab),
							QMessageBox::Ok);
						return;
					}
					// Unknown/Compatible: proceed to the
					// download as usual; the download dialog
					// keeps its own header probe as
					// defense-in-depth for flaky networks.
				}

				// OBS calls the filter update callback twice for a
				// single settings change (an immediate visual
				// callback and a deferred undo/redo callback ~500 ms
				// later). Skip duplicate download requests: two
				// concurrent downloads of the same file race for the
				// ".part" temp file and the loser reports a bogus
				// "failed to save" error for an ultimately
				// successful download.
				if (const std::string busy_model = model_download_in_progress();
				    !busy_model.empty()) {
					obs_log(
						LOG_WARNING,
						"Download of model '%s' is already in progress, skipping duplicate download request for '%s'",
						busy_model.c_str(),
						model_info.local_folder_name.c_str());
					return;
				}

				download_model_with_ui_dialog(
					model_info, [gf, new_model_path,
						     model_info](int download_status,
								 const std::string &path) {
						if (download_status == 0) {
							obs_log(LOG_INFO,
								"Model download complete");

							// The vocabulary was not
							// verifiable before the
							// download (no local file);
							// refuse here instead of
							// loading a model that
							// would crash OBS.
							const int64_t n_vocab =
								read_ggml_model_n_vocab(
									path);
							if (!is_whisper_vocab_compatible(
								    n_vocab)) {
								obs_log(
									LOG_ERROR,
									"Downloaded model file '%s' has a truncated vocabulary (n_vocab=%lld, expecting >= %lld). Such pruned models are incompatible with whisper.cpp and crash OBS when decoding; not loading it.",
									path.c_str(),
									(long long)n_vocab,
									(long long)kMinCompatibleWhisperVocab);
								return;
							}

							// Free the previous model
							// only now, immediately
							// before starting the new
							// one.
							shutdown_whisper_thread(gf);
							gf->whisper_model_path =
								new_model_path;
							start_whisper_thread_with_path(
								gf, path);
						} else {
							obs_log(
								LOG_ERROR,
								"Model download failed, keeping the previously loaded model '%s'",
								gf->whisper_model_path
									.c_str());
							// Revert the setting to
							// the model that is
							// actually loaded, so the
							// next settings update or
							// OBS restart does not try
							// to download the failed
							// model again.
							obs_data_t *settings =
								obs_source_get_settings(
									gf->context);
							if (settings != nullptr) {
								obs_data_set_string(
									settings,
									"whisper_model_path",
									gf->whisper_model_path
										.c_str());
								obs_data_release(
									settings);
							}
						}
					});
			} else {
				// Model exists, just load it: free the previous
				// model immediately before starting the new one.
				shutdown_whisper_thread(gf);
				gf->whisper_model_path = new_model_path;
				start_whisper_thread_with_path(gf, model_file_found);
			}
		} else {
			// new model is external file, get file location from file property
			std::string external_model_file_path =
				obs_data_get_string(s, "whisper_model_path_external");
			if (external_model_file_path.empty()) {
				obs_log(LOG_WARNING, "External model file path is empty");
			} else {
				// check if the external model file is not currently loaded
				if (gf->whisper_model_file_currently_loaded ==
				    external_model_file_path) {
					obs_log(LOG_INFO, "External model file is already loaded");
					return;
				} else {
					// The same honest refusal as for catalog
					// models: an external pruned-vocabulary
					// model would crash OBS the same way.
					const int64_t n_vocab = read_ggml_model_n_vocab(
						external_model_file_path);
					if (!is_whisper_vocab_compatible(n_vocab)) {
						obs_log(
							LOG_ERROR,
							"External model file '%s' has a truncated vocabulary (n_vocab=%lld, expecting >= %lld). Such pruned models are incompatible with whisper.cpp and crash OBS when decoding; not loading it.",
							external_model_file_path.c_str(),
							(long long)n_vocab,
							(long long)kMinCompatibleWhisperVocab);
						return;
					}

					shutdown_whisper_thread(gf);
					gf->whisper_model_path = new_model_path;
					start_whisper_thread_with_path(gf,
								       external_model_file_path);
				}
			}
		}
	} else {
		// model path did not change
		obs_log(gf->log_level, "Model path did not change: %s == %s",
			gf->whisper_model_path.c_str(), new_model_path.c_str());
	}
}

void shutdown_whisper_thread(struct pottymuter_data *gf)
{
	obs_log(gf->log_level, "shutdown_whisper_thread");
	if (gf->whisper_context != nullptr) {
		// acquire the mutex before freeing the context
		std::lock_guard<std::mutex> lock(gf->whisper_ctx_mutex);
		whisper_free(gf->whisper_context);
		gf->whisper_context = nullptr;
	}
	if (gf->whisper_thread.joinable()) {
		gf->whisper_thread.join();
	}
	if (!gf->whisper_model_path.empty()) {
		gf->whisper_model_path = "";
	}
}

void start_whisper_thread_with_path(struct pottymuter_data *gf, const std::string &path)
{
	obs_log(gf->log_level, "start_whisper_thread_with_path: %s", path.c_str());
	std::lock_guard<std::mutex> lock(gf->whisper_ctx_mutex);
	if (gf->whisper_context != nullptr) {
		obs_log(LOG_ERROR, "cannot init whisper: whisper_context is not null");
		return;
	}

	// initialize Silero VAD
	char *data_folder_models = obs_module_file("models");
	if (data_folder_models == nullptr) {
		obs_log(LOG_ERROR, "Failed to find models folder");
		return;
	}
	const std::filesystem::path module_data_models_folder =
		std::filesystem::absolute(data_folder_models);
	bfree(data_folder_models);
#ifdef _WIN32
	std::wstring silero_vad_model_path =
		module_data_models_folder.wstring() + L"\\silero-vad\\silero_vad.onnx";
	obs_log(gf->log_level, "silero vad model path: %ls", silero_vad_model_path.c_str());
#else
	std::string silero_vad_model_path =
		module_data_models_folder.string() + "/silero-vad/silero_vad.onnx";
	obs_log(gf->log_level, "silero vad model path: %s", silero_vad_model_path.c_str());
#endif
	// roughly following https://github.com/SYSTRAN/faster-whisper/blob/master/faster_whisper/vad.py
	// for silero vad parameters
	gf->vad.reset(new VadIterator(silero_vad_model_path, WHISPER_SAMPLE_RATE, 64, 0.5f, 1000,
				      200, 250));

	gf->whisper_context = init_whisper_context(path, gf);
	if (gf->whisper_context == nullptr) {
		obs_log(LOG_ERROR, "Failed to initialize whisper context");
		return;
	}
	gf->whisper_model_file_currently_loaded = path;
	std::thread new_whisper_thread(whisper_loop, gf);
	gf->whisper_thread.swap(new_whisper_thread);
}

// clang-format off
#define is_lead_byte(c) (((c) & 0xe0) == 0xc0 || ((c) & 0xf0) == 0xe0 || ((c) & 0xf8) == 0xf0)
// clang-format off
#define is_trail_byte(c) (((c) & 0xc0) == 0x80)

inline int lead_byte_length(const uint8_t c)
{
	if ((c & 0xe0) == 0xc0) {
		return 2;
	} else if ((c & 0xf0) == 0xe0) {
		return 3;
	} else if ((c & 0xf8) == 0xf0) {
		return 4;
	} else {
		return 1;
	}
}

inline bool is_valid_lead_byte(const uint8_t *c)
{
	const int length = lead_byte_length(c[0]);
	if (length == 1) {
		return true;
	}
	if (length == 2 && is_trail_byte(c[1])) {
		return true;
	}
	if (length == 3 && is_trail_byte(c[1]) && is_trail_byte(c[2])) {
		return true;
	}
	if (length == 4 && is_trail_byte(c[1]) && is_trail_byte(c[2]) && is_trail_byte(c[3])) {
		return true;
	}
	return false;
}

/*
* Fix UTF8 encoding issues on Windows.
*/
std::string fix_utf8(const std::string &str)
{
#ifdef _WIN32
	// Some UTF8 charsets on Windows output have a bug, instead of 0xd? it outputs
	// 0xf?, and 0xc? becomes 0xe?, so we need to fix it.
	std::stringstream ss;
	uint8_t *c_str = (uint8_t *)str.c_str();
	for (size_t i = 0; i < str.size(); ++i) {
		if (is_lead_byte(c_str[i])) {
			// this is a unicode leading byte
			// if the next char is 0xff - it's a bug char, replace it with 0x9f
			if (c_str[i + 1] == 0xff) {
				c_str[i + 1] = 0x9f;
			}
			if (!is_valid_lead_byte(c_str + i)) {
				// This is a bug lead byte, because it's length 3 and the i+2 byte is also
				// a lead byte
				c_str[i] = c_str[i] - 0x20;
			}
		} else {
			if (c_str[i] >= 0xf8) {
				// this may be a malformed lead byte.
				// lets see if it becomes a valid lead byte if we "fix" it
				uint8_t buf_[4];
				buf_[0] = c_str[i] - 0x20;
				buf_[1] = c_str[i + 1];
				buf_[2] = c_str[i + 2];
				buf_[3] = c_str[i + 3];
				if (is_valid_lead_byte(buf_)) {
					// this is a malformed lead byte, fix it
					c_str[i] = c_str[i] - 0x20;
				}
			}
		}
	}

	return std::string((char *)c_str);
#else
	return str;
#endif
}

