#ifndef MODEL_DOWNLOADER_H
#define MODEL_DOWNLOADER_H

#include <string>

#include "model-downloader-types.h"

std::string find_model_folder(const ModelInfo &model_info);
std::string find_model_bin_file(const ModelInfo &model_info);

// Start the model downloader UI dialog with a callback for when the download is finished
void download_model_with_ui_dialog(const ModelInfo &model_info,
				   download_finished_callback_t download_finished_callback);

// Returns an empty string when no download is running and reserves the slot
// for local_folder_name; otherwise returns the name of the model whose
// download is currently running (no reservation made).
std::string try_begin_model_download(const std::string &local_folder_name);
void end_model_download(const std::string &local_folder_name);
// Name of the model (local_folder_name) whose download is currently running,
// or an empty string when no download is active. Thread-safe.
std::string model_download_in_progress();

#endif // MODEL_DOWNLOADER_H
