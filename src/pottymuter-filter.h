#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

void pottymuter_activate(void *data);
void *pottymuter_create(obs_data_t *settings, obs_source_t *filter);
void pottymuter_update(void *data, obs_data_t *s);
void pottymuter_destroy(void *data);
const char *pottymuter_name(void *unused);
struct obs_audio_data *pottymuter_filter_audio(void *data, struct obs_audio_data *audio);
void pottymuter_deactivate(void *data);
void pottymuter_defaults(obs_data_t *s);
obs_properties_t *pottymuter_properties(void *data);

#ifdef __cplusplus
}
#endif
