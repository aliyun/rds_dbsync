/**
 * Copyright (c) 2015 rxi
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 */

#ifndef INI_H
#define INI_H

#define INI_VERSION "0.1.1"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ini_t ini_t;

extern ini_t*      ini_load(const char *filename);
extern void        ini_free(ini_t *ini);
extern const char* ini_get(ini_t *ini, const char *section, const char *key);
extern int         ini_sget(ini_t *ini, const char *section, const char *value,
                     const char *scanfmt, void *dst);
extern void *init_config(char *cfgpath);
extern int get_config(void *cfg, char *sec, char* key, char **value);

#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif
