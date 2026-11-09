#include "model-downloader.h"
#include "plugin-support.h"
#include "model-downloader-ui.h"
#include "model-find-utils.h"

#include <obs-module.h>
#include <obs-frontend-api.h>

std::string find_model_folder(const ModelInfo &model_info)
{
	if (model_info.friendly_name.empty()) {
		obs_log(LOG_ERROR, "Model info is invalid. Friendly name is empty.");
		return "";
	}
	if (model_info.local_folder_name.empty()) {
		obs_log(LOG_ERROR, "Model info is invalid. Local folder name is empty.");
		return "";
	}
	if (model_info.files.empty()) {
		obs_log(LOG_ERROR, "Model info is invalid. Files list is empty.");
		return "";
	}

	char *data_folder_models = obs_module_file("models");
	const std::filesystem::path module_data_models_folder =
		std::filesystem::absolute(data_folder_models);
	bfree(data_folder_models);

	const std::string model_local_data_path =
		(module_data_models_folder / model_info.local_folder_name).string();

	obs_log(LOG_INFO, "Checking if model '%s' exists in data...",
		model_info.friendly_name.c_str());

	if (!std::filesystem::exists(model_local_data_path)) {
		obs_log(LOG_INFO, "Model not found in data: %s", model_local_data_path.c_str());
	} else {
		obs_log(LOG_INFO, "Model folder found in data: %s", model_local_data_path.c_str());
		return model_local_data_path;
	}

	// Check if model exists in the config folder
	char *config_folder = obs_module_config_path("models");
	if (!config_folder) {
		obs_log(LOG_INFO, "Config folder not set.");
		return "";
	}
#ifdef _WIN32
	// convert mbstring to wstring
	const int config_len = (int)strlen(config_folder);
	int count = MultiByteToWideChar(CP_UTF8, 0, config_folder, config_len, NULL, 0);
	std::wstring config_folder_str(count, 0);
	MultiByteToWideChar(CP_UTF8, 0, config_folder, config_len, &config_folder_str[0], count);
	obs_log(LOG_INFO, "Config models folder: %S", config_folder_str.c_str());
#else
	std::string config_folder_str = config_folder;
	obs_log(LOG_INFO, "Config models folder: %s", config_folder_str.c_str());
#endif

	const std::filesystem::path module_config_models_folder =
		std::filesystem::absolute(config_folder_str);
	bfree(config_folder);

	obs_log(LOG_INFO, "Checking if model '%s' exists in config...",
		model_info.friendly_name.c_str());

	const std::string model_local_config_path =
		(module_config_models_folder / model_info.local_folder_name).string();

	obs_log(LOG_INFO, "Lookig for model in config: %s", model_local_config_path.c_str());
	if (std::filesystem::exists(model_local_config_path)) {
		obs_log(LOG_INFO, "Model folder exists in config folder: %s",
			model_local_config_path.c_str());
		return model_local_config_path;
	}

	obs_log(LOG_INFO, "Model '%s' not found.", model_info.friendly_name.c_str());
	return "";
}

std::string find_model_bin_file(const ModelInfo &model_info)
{
	const std::string model_local_folder_path = find_model_folder(model_info);
	if (model_local_folder_path.empty()) {
		return "";
	}

	return find_bin_file_in_folder(model_local_folder_path);
}

void download_model_with_ui_dialog(const ModelInfo &model_info,
				   download_finished_callback_t download_finished_callback)
{
	// Defense-in-depth: the caller (update_whisper_model) already skips
	// duplicate requests, but reserve the download slot synchronously on
	// this (UI) thread anyway, so two updates racing in can never both
	// reach this point with the slot free. The worker releases the slot
	// in its destructor.
	const std::string busy_model = try_begin_model_download(model_info.local_folder_name);
	if (!busy_model.empty()) {
		obs_log(LOG_WARNING,
			"Model download already in progress for '%s', ignoring new download request for '%s'",
			busy_model.c_str(), model_info.local_folder_name.c_str());
		return;
	}

	// Start the model downloader UI
	ModelDownloader *model_downloader = new ModelDownloader(
		model_info, download_finished_callback, (QWidget *)obs_frontend_get_main_window());
	model_downloader->show();
}
