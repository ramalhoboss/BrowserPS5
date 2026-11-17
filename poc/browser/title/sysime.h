/* Copyright (C) 2026 BrowserPS5 contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3, or (at your option) any
 * later version.
 *
 * The system keyboard's library, loaded and bound at run time (sysime.c).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sysime {
    int (*init)(const void *param, const void *extended);
    int (*get_status)(void);
    int (*get_result)(void *result);
    int (*term)(void);
    int (*abort)(void);                    /* may be 0 */
    int (*common_dialog_initialize)(void); /* may be 0 */
} sysime_t;

/* Loads libSceImeDialog (and libSceCommonDialog) into the title and binds their functions. Returns 0 when
 * init, get_status, get_result and term are all bound; every step goes to the title's report. */
int sysime_load(sysime_t *out);

#ifdef __cplusplus
}
#endif
