/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_media_browser.h"

#include <stdint.h>
#include <string.h>

static size_t path_length(const char *text)
{
    size_t length;
    if (text == NULL) return SIZE_MAX;
    for (length = 0U; length <= MT_MEDIA_BROWSER_PATH_MAX; ++length)
        if (text[length] == '\0') return length;
    return SIZE_MAX;
}

static bool valid_component(const char *text, size_t length)
{
    size_t index;
    if (length == 0U || length > MT_MEDIA_BROWSER_PATH_MAX ||
        text[length - 1U] == '.' || text[length - 1U] == ' ') return false;
    for (index = 0U; index < length; ++index) {
        const unsigned char ch = (unsigned char)text[index];
        if (ch < 32U || ch == 127U || strchr("/\\:*?\"<>|", (int)ch) != NULL)
            return false;
    }
    return true;
}

bool mt_media_browser_valid_component(const char *name)
{
    const size_t length = path_length(name);
    return length != SIZE_MAX && valid_component(name, length);
}

static size_t root_prefix(const char *root, size_t length)
{
    const unsigned char first = (unsigned char)root[0];
    if (length != 0U && root[0] == '/') return 1U;
    if (length >= 3U && root[1] == ':' && root[2] == '/' &&
        ((first >= '0' && first <= '9') || (first >= 'A' && first <= 'Z') ||
         (first >= 'a' && first <= 'z'))) return 3U;
    return 0U;
}

static bool valid_components(const char *path, size_t start, size_t length)
{
    size_t index;
    if (start == length) return true;
    for (index = start; index <= length; ++index) {
        if (index == length || path[index] == '/') {
            if (!valid_component(path + start, index - start)) return false;
            start = index + 1U;
        }
    }
    return true;
}

static bool valid_state(const mt_media_browser_t *browser)
{
    size_t root_length, relative_length;
    if (browser == NULL) return false;
    root_length = path_length(browser->root);
    relative_length = path_length(browser->relative);
    return root_length != 0U && root_length != SIZE_MAX &&
           relative_length != SIZE_MAX &&
           valid_components(browser->root, root_prefix(browser->root, root_length), root_length) &&
           valid_components(browser->relative, 0U, relative_length);
}

bool mt_media_browser_init(mt_media_browser_t *browser, const char *root)
{
    mt_media_browser_t candidate = {{0}, {0}};
    size_t length = path_length(root);
    size_t prefix;
    if (browser == NULL || length == SIZE_MAX || length == 0U) return false;
    prefix = root_prefix(root, length);
    if (length > prefix && root[length - 1U] == '/') --length;
    if (length == 0U || !valid_components(root, prefix, length)) return false;
    memcpy(candidate.root, root, length);
    *browser = candidate;
    return true;
}

bool mt_media_browser_path(const mt_media_browser_t *browser,
                           char *output, size_t capacity)
{
    char path[2U * MT_MEDIA_BROWSER_PATH_MAX + 2U];
    size_t root_length, relative_length, separator, length;
    if (!valid_state(browser) || output == NULL) return false;
    root_length = strlen(browser->root);
    relative_length = strlen(browser->relative);
    separator = relative_length != 0U && browser->root[root_length - 1U] != '/' ? 1U : 0U;
    length = root_length + separator + relative_length;
    if (capacity <= length) return false;
    memcpy(path, browser->root, root_length);
    if (separator != 0U) path[root_length] = '/';
    memcpy(path + root_length + separator, browser->relative, relative_length + 1U);
    memcpy(output, path, length + 1U);
    return true;
}

bool mt_media_browser_resolve(const mt_media_browser_t *browser, const char *child,
                              char *output, size_t capacity)
{
    char path[MT_MEDIA_BROWSER_PATH_MAX + 1U];
    size_t current_length, child_length, separator, length;
    if (!valid_state(browser) || !mt_media_browser_valid_component(child) || output == NULL)
        return false;
    current_length = strlen(browser->relative);
    child_length = strlen(child);
    separator = current_length != 0U ? 1U : 0U;
    length = current_length + separator + child_length;
    if (length > MT_MEDIA_BROWSER_PATH_MAX || capacity <= length) return false;
    memcpy(path, browser->relative, current_length);
    if (separator != 0U) path[current_length] = '/';
    memcpy(path + current_length + separator, child, child_length + 1U);
    memcpy(output, path, length + 1U);
    return true;
}

bool mt_media_browser_enter(mt_media_browser_t *browser, const char *child)
{
    char relative[MT_MEDIA_BROWSER_PATH_MAX + 1U];
    if (!mt_media_browser_resolve(browser, child, relative, sizeof(relative))) return false;
    memcpy(browser->relative, relative, strlen(relative) + 1U);
    return true;
}

bool mt_media_browser_can_up(const mt_media_browser_t *browser)
{
    return valid_state(browser) && browser->relative[0] != '\0';
}

bool mt_media_browser_up(mt_media_browser_t *browser)
{
    char *separator;
    if (!mt_media_browser_can_up(browser)) return false;
    separator = strrchr(browser->relative, '/');
    if (separator != NULL) *separator = '\0';
    else browser->relative[0] = '\0';
    return true;
}

/* Finder resource forks and other dot entries are not user media. Parent
 * navigation is an explicit UI row, never an SD directory entry. */
bool mt_media_browser_visible_component(const char *name)
{
    return mt_media_browser_valid_component(name) && name[0] != '.';
}
