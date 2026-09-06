#ifndef YM2612_FORMAT_C_API_H
#define YM2612_FORMAT_C_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Library version ("0.3.0"). Static storage; do not free. */
const char *ym2612_version(void);

/* JSON array of format descriptors:
 * [{"format":"dmp","name":"DefleMask Preset","extension":"dmp",
 *   "can_read":true,"can_write":true,"is_text":false}, ...]
 * Free with ym2612_free. */
char *ym2612_formats_json(void);

/* Parse instrument data.
 * name:   fallback patch name; its extension is the format hint when format is NULL.
 * format: extension or format name to force, or NULL.
 * Success: JSON {"patches":[{"name":"...","algorithm":0,"feedback":0,
 *          "has_macros":false,"mml":"@1 fm ...\n..."}],"warnings":["..."]},
 *          *error left untouched.
 * Failure: NULL; *error receives a message (free with ym2612_free) when error is non-NULL. */
char *ym2612_parse_json(const uint8_t *data, size_t size, const char *name,
                        const char *format, char **error);

/* Parse, then serialize patch `index` to `target_format`.
 * Returns bytes (free with ym2612_free), length in *out_size.
 * Failure: NULL, *out_size = 0, *error as above. */
uint8_t *ym2612_convert(const uint8_t *data, size_t size, const char *name,
                        const char *format, size_t index,
                        const char *target_format, size_t *out_size,
                        char **error);

void ym2612_free(void *ptr);

#ifdef __cplusplus
}
#endif
#endif
