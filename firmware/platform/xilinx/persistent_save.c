/* SPDX-License-Identifier: GPL-3.0-only */
#include "persistent_save.h"

#include <string.h>

static mt_persistent_save_result_t save_result(
    mt_persistent_save_status_t status, mt_sd_result_t sd)
{
    mt_persistent_save_result_t result;
    result.status = status;
    result.sd = sd;
    return result;
}

static mt_sd_result_t empty_sd_result(void)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    return result;
}

static bool valid_component(const char *name)
{
    size_t index;
    if (name == NULL || name[0] == '\0' || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) return false;
    for (index = 0U; name[index] != '\0'; ++index) {
        if (name[index] == '/' || name[index] == '\\' || name[index] == ':')
            return false;
    }
    return true;
}

static bool valid_root(const char *root)
{
    const size_t length = root == NULL ? 0U : strlen(root);
    return length >= 3U && root[0] >= '0' && root[0] <= '9' &&
           root[1] == ':' && root[2] == '/' && strstr(root, "..") == NULL;
}

static bool build_paths(mt_persistent_save_t *save, const char *root,
                        const char *name)
{
    static const char save_suffix[] = ".sav";
    static const char journal_suffix[] = ".mtj";
    const size_t root_size = strlen(root);
    const size_t name_size = strlen(name);
    const size_t save_size = root_size + 1U + name_size + sizeof(save_suffix);
    const size_t journal_size = save_size - 1U + sizeof(journal_suffix);
    if (save_size > sizeof(save->path) ||
        journal_size > sizeof(save->journal_path)) return false;
    memcpy(save->path, root, root_size);
    save->path[root_size] = '/';
    memcpy(save->path + root_size + 1U, name, name_size);
    memcpy(save->path + root_size + 1U + name_size,
           save_suffix, sizeof(save_suffix));
    memcpy(save->journal_path, save->path, save_size);
    memcpy(save->journal_path + save_size - 1U,
           journal_suffix, sizeof(journal_suffix));
    return true;
}

mt_persistent_save_result_t mt_persistent_save_open(
    mt_persistent_save_t *save, const char *content_root,
    const char *content_name, uint8_t *data, size_t size,
    uint8_t erased_value)
{
    mt_sd_result_t sd;
    bool found;
    if (save == NULL || data == NULL || size == 0U ||
        !valid_root(content_root) || !valid_component(content_name)) {
        return save_result(MT_PERSISTENT_SAVE_INVALID_ARGUMENT,
                           empty_sd_result());
    }
    memset(save, 0, sizeof(*save));
    if (!build_paths(save, content_root, content_name)) {
        return save_result(MT_PERSISTENT_SAVE_INVALID_ARGUMENT,
                           empty_sd_result());
    }
    sd = mt_sd_storage_journal_recover(
        save->path, save->journal_path, data, size);
    if (sd.status != MT_SD_OK) {
        return save_result(MT_PERSISTENT_SAVE_RECOVERY_FAILED, sd);
    }
    sd = mt_sd_storage_read_file_optional_exact(
        save->path, data, size, &found);
    if (sd.status != MT_SD_OK) {
        return save_result(MT_PERSISTENT_SAVE_READ_FAILED, sd);
    }
    if (!found) memset(data, erased_value, size);
    save->size = size;
    save->open = true;
    save->existed = found;
    return save_result(MT_PERSISTENT_SAVE_OK, sd);
}

mt_persistent_save_result_t mt_persistent_save_commit(
    mt_persistent_save_t *save, const uint8_t *data, size_t size)
{
    mt_sd_result_t sd;
    if (save == NULL || data == NULL || !save->open ||
        size == 0U || size != save->size) {
        return save_result(MT_PERSISTENT_SAVE_INVALID_ARGUMENT,
                           empty_sd_result());
    }
    sd = mt_sd_storage_journal_replace(
        save->path, save->journal_path, data, size);
    if (sd.status != MT_SD_OK)
        return save_result(MT_PERSISTENT_SAVE_COMMIT_FAILED, sd);
    save->existed = true;
    return save_result(MT_PERSISTENT_SAVE_OK, sd);
}

void mt_persistent_save_close(mt_persistent_save_t *save)
{
    if (save != NULL) memset(save, 0, sizeof(*save));
}

const char *mt_persistent_save_status_name(mt_persistent_save_status_t status)
{
    switch (status) {
    case MT_PERSISTENT_SAVE_OK: return "ok";
    case MT_PERSISTENT_SAVE_INVALID_ARGUMENT: return "invalid argument";
    case MT_PERSISTENT_SAVE_RECOVERY_FAILED: return "recovery failed";
    case MT_PERSISTENT_SAVE_READ_FAILED: return "read failed";
    case MT_PERSISTENT_SAVE_COMMIT_FAILED: return "commit failed";
    }
    return "unknown persistent-save status";
}
