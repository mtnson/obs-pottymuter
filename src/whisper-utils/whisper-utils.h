#ifndef WHISPER_UTILS_H
#define WHISPER_UTILS_H

#include "pottymuter-filter-data.h"

#include <obs.h>

#include <string>

void update_whisper_model(struct pottymuter_data *gf, obs_data_t *s);
void shutdown_whisper_thread(struct pottymuter_data *gf);
void start_whisper_thread_with_path(struct pottymuter_data *gf, const std::string &path);
std::string fix_utf8(const std::string &str);
std::string remove_leading_trailing_nonalpha(const std::string &str);

#endif /* WHISPER_UTILS_H */
