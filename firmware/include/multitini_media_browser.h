/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_MEDIA_BROWSER_H
#define MULTITINI_MEDIA_BROWSER_H

#include <stdbool.h>
#include <stddef.h>

#define MT_MEDIA_BROWSER_PATH_MAX 255U

typedef struct {
    char root[MT_MEDIA_BROWSER_PATH_MAX + 1U];
    char relative[MT_MEDIA_BROWSER_PATH_MAX + 1U];
} mt_media_browser_t;

/* Portable path state only; directory enumeration remains with the caller.
 * Root and relative paths each contain at most 255 bytes, excluding NUL.
 * Root may be relative, absolute, or have a one-character volume prefix.
 * A final root slash is normalized. Child names are single FAT-safe components.
 * Failed calls leave state and output unchanged. No operation traverses above
 * root. The backend must not follow filesystem links outside that root. */
bool mt_media_browser_init(mt_media_browser_t *browser, const char *root);
bool mt_media_browser_valid_component(const char *name);
/* Join a nonempty root-relative file path. Every component is validated;
 * absolute paths, dot components and empty components are rejected. */
bool mt_media_browser_join(const char *root, const char *relative,
                           char *output, size_t capacity);
bool mt_media_browser_path(const mt_media_browser_t *browser,
                           char *output, size_t capacity);
bool mt_media_browser_resolve(const mt_media_browser_t *browser, const char *child,
                              char *output, size_t capacity);
bool mt_media_browser_enter(mt_media_browser_t *browser, const char *child);
bool mt_media_browser_up(mt_media_browser_t *browser);
bool mt_media_browser_can_up(const mt_media_browser_t *browser);

bool mt_media_browser_visible_component(const char *name);

#endif
