#include "model-downloader-ui.h"
#include "plugin-support.h"
#include "sha256.h"
#include "ggml-header.h"
#include "vocab-probe.h"

#include <obs-module.h>

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	size_t written = fwrite(ptr, size, nmemb, stream);
	return written;
}

namespace {

// Every ggml whisper model is well above 1 MB; a smaller ".bin" is a broken
// or partial download.
constexpr std::uintmax_t kMinBinFileSize = 1024 * 1024;
// Metadata files (tokenizer configs etc.) are tiny but never empty.
constexpr std::uintmax_t kMinMetadataFileSize = 1024;

std::string lower_case(std::string s)
{
	for (char &c : s)
		c = char(tolower((unsigned char)c));
	return s;
}

bool is_bin_file(const std::filesystem::path &path)
{
	return (path.extension() == ".bin");
}

// Registry of the currently running model download, used to reject duplicate
// download requests. OBS calls the filter update callback twice for a single
// settings change (an immediate visual callback and a deferred undo/redo
// callback ~500 ms later); without the registry both invocations download the
// same file concurrently, race for the ".part" temp file and the loser
// reports a bogus "failed to save" error for an ultimately successful
// download.
//
// The reservation is made synchronously on the UI thread (from
// download_model_with_ui_dialog, see model-downloader.cpp) while the
// registry entry is released by the download worker's destructor, so it stays
// consistent across every exit path - success, error, and also when the
// dialog is destroyed mid-download (closing the "Filters" window) and its
// completion callback is never delivered.
std::mutex &active_download_mutex()
{
	static std::mutex m;
	return m;
}

std::string &active_download_model()
{
	static std::string model;
	return model;
}

} // namespace

std::string try_begin_model_download(const std::string &local_folder_name)
{
	std::lock_guard<std::mutex> lock(active_download_mutex());
	std::string &current = active_download_model();
	if (!current.empty())
		return current;
	current = local_folder_name;
	return {};
}

void end_model_download(const std::string &local_folder_name)
{
	std::lock_guard<std::mutex> lock(active_download_mutex());
	if (active_download_model() == local_folder_name)
		active_download_model().clear();
}

std::string model_download_in_progress()
{
	std::lock_guard<std::mutex> lock(active_download_mutex());
	return active_download_model();
}

// A file is considered a complete artifact when its size is plausible and
// (when the model catalog provides a checksum) its SHA-256 matches.
// The catalog normaly stores uppercase digests; compare case-insensitively.
bool file_matches(const std::filesystem::path &path, const std::string &expected_sha256)
{
	std::uintmax_t size = 0;
	try {
		size = std::filesystem::file_size(path);
	} catch (const std::filesystem::filesystem_error &) {
		return false;
	}

	if (is_bin_file(path) ? size < kMinBinFileSize : size < kMinMetadataFileSize) {
		obs_log(LOG_WARNING, "File size is implausible (0 or partial): %s (%llu bytes)",
			path.string().c_str(), (unsigned long long)size);
		return false;
	}

	if (expected_sha256.empty())
		return true;

	const char *actual = sha256_file_hex(path.string().c_str());
	if (actual[0] == '\0') { // hashing failed (self-test or IO error)
		return false;
	}
	const bool matches = lower_case(actual) == lower_case(expected_sha256);
	if (!matches)
		obs_log(LOG_WARNING, "SHA-256 mismatch for %s", path.string().c_str());
	return matches;
}


ModelDownloader::ModelDownloader(const ModelInfo &model_info,
				 download_finished_callback_t download_finished_callback_,
				 QWidget *parent)
	: QDialog(parent),
	  download_finished_callback(download_finished_callback_)
{
	this->setWindowTitle("Downloading model...");
	this->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
	this->setFixedSize(300, 100);
	// Bring the dialog to the front
	this->activateWindow();
	this->raise();

	this->layout = new QVBoxLayout(this);

	// Add a label for the model name
	QLabel *model_name_label = new QLabel(this);
	model_name_label->setText(QString::fromStdString(model_info.friendly_name));
	model_name_label->setAlignment(Qt::AlignCenter);
	this->layout->addWidget(model_name_label);

	this->progress_bar = new QProgressBar(this);
	this->progress_bar->setRange(0, 100);
	this->progress_bar->setValue(0);
	this->progress_bar->setAlignment(Qt::AlignCenter);
	// Show progress as a percentage
	this->progress_bar->setFormat("%p%");
	this->layout->addWidget(this->progress_bar);

	this->download_thread = new QThread();
	this->download_worker = new ModelDownloadWorker(model_info);
	this->download_worker->moveToThread(this->download_thread);

	connect(this->download_thread, &QThread::started, this->download_worker,
		&ModelDownloadWorker::download_model);
	connect(this->download_worker, &ModelDownloadWorker::download_progress, this,
		&ModelDownloader::update_progress);
	connect(this->download_worker, &ModelDownloadWorker::download_finished, this,
		&ModelDownloader::download_finished);
	connect(this->download_worker, &ModelDownloadWorker::download_finished,
		this->download_thread, &QThread::quit);
	connect(this->download_worker, &ModelDownloadWorker::download_finished,
		this->download_worker, &ModelDownloadWorker::deleteLater);
	connect(this->download_worker, &ModelDownloadWorker::download_error, this,
		&ModelDownloader::show_error);
	connect(this->download_thread, &QThread::finished, this->download_thread,
		&QThread::deleteLater);

	this->download_thread->start();
}

void ModelDownloader::closeEvent(QCloseEvent *e)
{
	if (!this->mPrepareToClose)
		e->ignore();
	else {
		QDialog::closeEvent(e);
		deleteLater();
	}
}

void ModelDownloader::close()
{
	this->mPrepareToClose = true;

	QDialog::close();
}

void ModelDownloader::update_progress(int progress)
{
	this->progress_bar->setValue(progress);
}

void ModelDownloader::download_finished(const std::string &path)
{
	// Call the callback with the path to the downloaded model
	this->download_finished_callback(0, path);
	// Close the dialog
	this->close();
}

void ModelDownloader::show_error(const std::string &reason)
{
	this->setWindowTitle("Download failed!");
	this->progress_bar->setFormat("Download failed!");
	this->progress_bar->setAlignment(Qt::AlignCenter);
	this->progress_bar->setStyleSheet("QProgressBar::chunk { background-color: #FF0000; }");
	// Add a label to show the error
	QLabel *error_label = new QLabel(this);
	error_label->setText(QString::fromStdString(reason));
	error_label->setAlignment(Qt::AlignCenter);
	// Wrap long reasons (e.g. HTTP codes + hints) instead of stretching the window
	error_label->setWordWrap(true);
	// Color red
	error_label->setStyleSheet("QLabel { color : red; }");
	this->layout->addWidget(error_label);
	// Add a button to close the dialog
	QPushButton *close_button = new QPushButton("Close", this);
	this->layout->addWidget(close_button);
	connect(close_button, &QPushButton::clicked, this, &ModelDownloader::close);
	// The constructor locked the dialog to (300x100) for the progress-only
	// state; unlock it and shrink-to-fit so progress bar, error text and the
	// close button are all fully visible instead of being squished/cut off.
	// A wider minimum is needed because long refusal reasons wrap into
	// several lines at 300px and get clipped; activate the layout before
	// measuring, otherwise adjustSize() uses the stale size hint.
	this->setMinimumSize(440, 140);
	this->setMaximumSize(520, 340);
	this->layout->activate();
	this->adjustSize();
	this->download_finished_callback(1, "");
}

ModelDownloadWorker::ModelDownloadWorker(const ModelInfo &model_info_) : model_info(model_info_) {}

std::string get_filename_from_url(const std::string &url)
{
	auto lastSlashPos = url.find_last_of("/");
	auto queryPos = url.find("?", lastSlashPos);
	if (queryPos == std::string::npos) {
		return url.substr(lastSlashPos + 1);
	} else {
		return url.substr(lastSlashPos + 1, queryPos - lastSlashPos - 1);
	}
}

void ModelDownloadWorker::download_model()
{
	char *config_folder = obs_module_config_path("models");
#ifdef _WIN32
	// convert mbstring to wstring
	int count =
		MultiByteToWideChar(CP_UTF8, 0, config_folder, (int)strlen(config_folder), NULL, 0);
	std::wstring config_folder_str(count, 0);
	MultiByteToWideChar(CP_UTF8, 0, config_folder, (int)strlen(config_folder),
			    &config_folder_str[0], count);
	obs_log(LOG_INFO, "Download: Config models folder: %S", config_folder_str.c_str());
#else
	std::string config_folder_str = config_folder;
	obs_log(LOG_INFO, "Download: Config models folder: %s", config_folder_str.c_str());
#endif
	bfree(config_folder);

	const std::filesystem::path module_config_models_folder =
		std::filesystem::absolute(config_folder_str);

	// Check if the config folder exists
	if (!std::filesystem::exists(module_config_models_folder)) {
		obs_log(LOG_WARNING, "Config folder does not exist: %s",
			module_config_models_folder.string().c_str());
		// Create the config folder
		if (!std::filesystem::create_directories(module_config_models_folder)) {
			obs_log(LOG_ERROR, "Failed to create config folder: %s",
				module_config_models_folder.string().c_str());
			emit download_error("Failed to create config folder.");
			return;
		}
	}

	const std::string model_local_config_path =
		(module_config_models_folder / model_info.local_folder_name).string();

	obs_log(LOG_INFO, "Model save path: %s", model_local_config_path.c_str());

	if (!std::filesystem::exists(model_local_config_path)) {
		// model folder does not exist, create it
		if (!std::filesystem::create_directories(model_local_config_path)) {
			obs_log(LOG_ERROR, "Failed to create model folder: %s",
				model_local_config_path.c_str());
			emit download_error("Failed to create model folder.");
			return;
		}
	}

	CURL *curl = curl_easy_init();
	if (curl == nullptr) {
		obs_log(LOG_ERROR, "Failed to initialize curl.");
		emit download_error("Failed to initialize curl.");
		return;
	}

	for (auto &model_download_file : this->model_info.files) {
		obs_log(LOG_INFO, "Model URL: %s", model_download_file.url.c_str());

		const std::string model_filename =
			get_filename_from_url(model_download_file.url);
		const std::filesystem::path model_file_save_path =
			std::filesystem::path(model_local_config_path) / model_filename;

		// An existing file only counts as "downloaded" when it passes
		// validation (plausible size + sha256 when the catalog has one).
		// Partial or corrupted leftovers - e.g. a 0-byte file from a
		// previously failed download - are deleted and re-downloaded.
		if (std::filesystem::exists(model_file_save_path)) {
			if (file_matches(model_file_save_path, model_download_file.sha256)) {
				obs_log(LOG_INFO, "Model file already exists and is valid: %s",
					model_file_save_path.string().c_str());
				continue;
			}
			obs_log(LOG_WARNING,
				"Existing model file failed validation, will re-download: %s",
				model_file_save_path.string().c_str());
			std::error_code remove_ec;
			std::filesystem::remove(model_file_save_path, remove_ec);
		}

		// Read the remote ggml header before spending the transfer.
		// Only the ".bin" whisper model needs the probe; metadata
		// files (tokenizer configs etc.) are not whisper models.
		if (model_filename.size() >= 4 &&
		    model_filename.compare(model_filename.size() - 4, 4, ".bin") == 0) {
			int64_t remote_n_vocab = -1;
			if (probe_ggml_vocabulary(model_download_file.url,
						  &remote_n_vocab) == ProbeStatus::Pruned) {
				obs_log(LOG_ERROR,
					"Model file '%s' has a truncated vocabulary (n_vocab=%lld, expecting >= %lld); refusing to download it - the model is incompatible with whisper.cpp and would crash OBS",
					model_filename.c_str(),
					(long long)remote_n_vocab,
					(long long)kMinCompatibleWhisperVocab);
				curl_easy_cleanup(curl);
				emit download_error(
					"Incompatible model: truncated vocabulary (n_vocab=" +
					std::to_string(remote_n_vocab) +
					", standard models have >= " +
					std::to_string(kMinCompatibleWhisperVocab) +
					"). The download was canceled before any transfer.");
				return;
			}
			// Unknown/Compatible: continue; the downloaded file
			// still passes the local vocabulary check before it
			// is ever loaded into whisper.
		}

		// Download into a temporary ".part" file: a failed or partial
		// transfer must never leave something on disk that later looks
		// like a valid model file.
		const std::filesystem::path part_path =
			model_file_save_path.string() + ".part";
		FILE *fp = fopen(part_path.string().c_str(), "wb");
		if (fp == nullptr) {
			obs_log(LOG_ERROR, "Failed to open model file for writing %s.",
				part_path.string().c_str());
			curl_easy_cleanup(curl);
			emit download_error("Failed to open file for writing.");
			return;
		}

		curl_easy_setopt(curl, CURLOPT_URL, model_download_file.url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
				 ModelDownloadWorker::progress_callback);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
		// Follow redirects
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		// Treat HTTP >= 400 as an error: without this, a 403/404 error
		// page (e.g. a geo-block or captcha HTML) would be saved as the
		// "model" and pass the CURLE_OK check.
		curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

		CURLcode res = curl_easy_perform(curl);

		long http_code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		fclose(fp);

		if (res != CURLE_OK) {
			obs_log(LOG_ERROR, "Failed to download model file %s: %s (curl=%d, HTTP %ld)",
				model_filename.c_str(), curl_easy_strerror(res), (int)res,
				http_code);
			std::error_code remove_ec;
			std::filesystem::remove(part_path, remove_ec);
			curl_easy_cleanup(curl);

			std::string reason = "Failed to download model file '" + model_filename +
					     "'";
			if (http_code > 0)
				reason += ": HTTP " + std::to_string(http_code) +
					  " (the host may be unreachable from your network)";
			else
				reason += std::string(": ") + curl_easy_strerror(res);
			emit download_error(reason);
			return;
		}

		// Validate the downloaded file BEFORE it is allowed to count as
		// a model: size plausibility and (when the catalog provides it)
		// sha256.
		if (!file_matches(part_path, model_download_file.sha256)) {
			obs_log(LOG_ERROR, "Downloaded file failed validation: %s",
				part_path.string().c_str());
			std::error_code remove_ec;
			std::filesystem::remove(part_path, remove_ec);
			curl_easy_cleanup(curl);
			emit download_error("Downloaded model file is corrupted (failed size or "
					    "checksum validation), please try again.");
			return;
		}

		// Promote the validated temp file to its final name.
		std::error_code rename_ec;
		std::filesystem::rename(part_path, model_file_save_path, rename_ec);
		if (rename_ec) {
			obs_log(LOG_ERROR, "Failed to move downloaded file into place: %s (%s)",
				rename_ec.message().c_str(), model_file_save_path.string().c_str());
			std::error_code remove_ec;
			std::filesystem::remove(part_path, remove_ec);
			curl_easy_cleanup(curl);
			emit download_error("Failed to save downloaded model file.");
			return;
		}

		obs_log(LOG_INFO, "Model file downloaded and validated: %s",
			model_file_save_path.string().c_str());
	}

	curl_easy_cleanup(curl);
	emit download_finished(model_local_config_path);
}

int ModelDownloadWorker::progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
					   curl_off_t, curl_off_t)
{
	if (dltotal == 0) {
		return 0; // Unknown progress
	}
	ModelDownloadWorker *worker = (ModelDownloadWorker *)clientp;
	if (worker == nullptr) {
		obs_log(LOG_ERROR, "Worker is null.");
		return 1;
	}
	int progress = (int)(dlnow * 100l / dltotal);
	emit worker->download_progress(progress);
	return 0;
}

ModelDownloader::~ModelDownloader()
{
	if (this->download_thread != nullptr) {
		if (this->download_thread->isRunning()) {
			this->download_thread->quit();
			this->download_thread->wait();
		}
		delete this->download_thread;
	}
	if (this->download_worker != nullptr) {
		delete this->download_worker;
	}
}

ModelDownloadWorker::~ModelDownloadWorker()
{
	// Release the download registry even when the completion callback was
	// never delivered (e.g. the dialog was destroyed mid-download).
	end_model_download(this->model_info.local_folder_name);
}
