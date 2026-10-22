/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the board's real static handlers. The focused runner uses whole-
 * program LTO and section GC to discard unrelated hardware startup/services. */
#define main mt_unused_board_main
#include "../../firmware/platform/xilinx/main.c"
#undef main

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t test_core_id;
static uint32_t test_abi;
static uint32_t test_border;
static uint32_t test_vectrex_video;
static unsigned test_mmio_writes;
static unsigned test_save_calls;
static mt_sd_status_t test_save_status;
static unsigned test_effect_calls;
static bool test_effect_available;
static mt_video_effects_settings_t test_effects;
static bool test_display_enabled,test_display_fail,test_display_timeout;
static uint32_t test_display_status,test_display_last_request,test_clock_ms;
static unsigned test_display_writes,test_display_invalidations;

bool mt_video_effect_service_available(void) { return test_effect_available; }
void mt_video_effect_service_configure(bool supported, const mt_video_effects_settings_t *settings)
{
    if (supported && settings != NULL) test_effects = *settings;
    ++test_effect_calls;
}

static _Noreturn void unexpected_service(void)
{
    assert(!"Unrelated media/control service reached by video-only restore");
    abort();
}

/* GCC versions differ in which unreachable branches they discard after
 * importing main.c. Keep their dependencies linkable, but fail if a video
 * settings test actually reaches a timer, mode switch, USB or media action. */
void XTime_GetTime(XTime *value)
{
    if(test_display_enabled) {
        test_clock_ms+=100U;
        *value=(XTime)((uint64_t)test_clock_ms*COUNTS_PER_SECOND/1000U);
        return;
    }
    unexpected_service();
}

void mt_video_effect_service_suspend(void) { if(!test_display_enabled)unexpected_service(); }
bool mt_video_effect_service_quiescent(void) { if(!test_display_enabled)unexpected_service();return true; }
void usb_hid_service_poll_deferred(void) { if(!test_display_enabled)unexpected_service(); }

int usb_hid_service_set_symbolic_keyboard(bool enabled)
{
    (void)enabled;
    unexpected_service();
}

int usb_hid_service_set_joystick_port(uint8_t port)
{
    (void)port;
    unexpected_service();
}

void mt_ui_presenter_invalidate(mt_ui_presenter_t *presenter)
{
    (void)presenter;
    if(!test_display_enabled)unexpected_service();
    ++test_display_invalidations;
}

int c64_d64_service_prepare_reset(void) { unexpected_service(); }
int c64_d64_service_resume_after_reset(void) { unexpected_service(); }

/* This fixture exercises video settings; its hardware exposes no C64 expansion. */
uint32_t c64_content_service_expansion_caps(void) { return 0U; }
bool c64_content_service_reu_enabled(void) { return false; }

mt_c64_content_service_result_t c64_content_service_load(const char *name)
{
    (void)name;
    unexpected_service();
}

const char *c64_content_service_status_name(mt_c64_content_service_status_t status)
{
    (void)status;
    unexpected_service();
}

mt_content_type_t mt_content_classify(const char *path)
{
    (void)path;
    unexpected_service();
}

const char *mt_content_validation_name(mt_content_validation_t validation)
{
    (void)validation;
    unexpected_service();
}

const char *mt_c64_crt_hardware_name(uint16_t hardware_type)
{
    (void)hardware_type;
    unexpected_service();
}

mt_link_result_t mt_core_link_command(mt_core_link_t *link, uint32_t opcode,
    const uint32_t args[4], uint32_t max_polls, mt_link_progress_fn progress,
    void *progress_context, mt_command_response_t *response)
{
    (void)link; (void)opcode; (void)args; (void)max_polls; (void)progress;
    (void)progress_context; (void)response;
    unexpected_service();
}

const char *mt_link_result_name(mt_link_result_t result)
{
    (void)result;
    unexpected_service();
}

mt_gameboy_content_service_result_t gameboy_content_service_load(const char *name)
{
    (void)name;
    unexpected_service();
}

const char *gameboy_content_service_status_name(mt_gameboy_content_service_status_t status)
{
    (void)status;
    unexpected_service();
}

mt_vectrex_content_result_t vectrex_content_service_load(const char *name)
{
    (void)name;
    unexpected_service();
}

const mt_genesis_config_t *genesis_content_service_options(void)
{
    unexpected_service();
}

int genesis_content_service_set_options(const mt_genesis_config_t *options)
{
    (void)options;
    unexpected_service();
}

void usb_hid_service_set_core_ready(bool ready)
{
    (void)ready;
    unexpected_service();
}

uint32_t Xil_In32(uintptr_t address)
{
    if (address == MT_SHELL_BASE_ADDRESS + MT_REG_CORE_ID) return test_core_id;
    if (address == MT_SHELL_BASE_ADDRESS + MT_REG_ABI_VERSION) return test_abi;
    /* This fixture exercises per-core controls without a mode-switch service. */
    if (address == MT_SHELL_BASE_ADDRESS + MT_REG_DISPLAY_MODES) return test_display_enabled ? 63U:0U;
    if (address == MT_SHELL_BASE_ADDRESS + MT_REG_DISPLAY_STATUS && test_display_enabled) return test_display_status;
    assert(!"Unexpected MMIO read");
    return 0U;
}

void Xil_Out32(uintptr_t address, uint32_t value)
{
    ++test_mmio_writes;
    if (address == MT_SHELL_BASE_ADDRESS + MT_REG_BORDER_CONFIG) test_border = value;
    else if (address == MT_SHELL_BASE_ADDRESS + MT_REG_VECTREX_VIDEO_CONFIG) test_vectrex_video = value;
    else if(address==MT_SHELL_BASE_ADDRESS+MT_REG_DISPLAY_REQUEST && test_display_enabled) {
        assert((value&15U)<MT_DISPLAY_MODE_COUNT && (value>>4U)<=4U);
        if((test_abi&0xffffU)<21U)assert((value&MT_DISPLAY_SCALE_MASK)==0U);
        ++test_display_writes;test_display_last_request=value;
        test_display_status=test_display_timeout ? MT_DISPLAY_BUSY : test_display_fail ? MT_DISPLAY_ERROR:value;
    }
    else assert(!"Unexpected MMIO write");
}

int xil_printf(const char *format, ...)
{
    (void)format;
    return 0;
}

const char *mt_sd_status_name(mt_sd_status_t status)
{
    (void)status;
    return "mock storage";
}

mt_sd_result_t mt_sd_storage_journal_replace(const char *path,
    const char *journal_path, const uint8_t *source, size_t size)
{
    const mt_sd_result_t result = {test_save_status, 0U, size};
    assert(strcmp(path, MT_SETTINGS_PATH) == 0);
    assert(strcmp(journal_path, MT_SETTINGS_JOURNAL_PATH) == 0);
    assert(source != NULL && source[8U] == 10U && size == MT_SETTINGS_WIRE_SIZE);
    /* The visible hardware state must still have its previous value while
     * the complete settings write is in progress. */
    if(test_display_enabled)assert((test_display_status&(MT_DISPLAY_BUSY|MT_DISPLAY_ERROR))==0U && test_display_writes>0U);
    else assert(test_mmio_writes == 0U);
    ++test_save_calls;
    return result;
}

static void test_reset(uint32_t core_id, const char *id)
{
    memset(&supervisor, 0, sizeof(supervisor));
    supervisor.has_active = true;
    supervisor.active.package.core_id = core_id;
    supervisor.active.package.abi_minor = MT_ABI_MINOR;
    strcpy(supervisor.active.package.id, id);
    supervisor.active.package.variant_count = 1U;
    strcpy(supervisor.active.package.variants[0].id, "default");
    test_core_id = core_id;
    test_abi = MT_ABI_VERSION;
    test_border = UINT32_C(0x670);
    test_vectrex_video = UINT32_C(0xB);
    test_mmio_writes = test_save_calls = 0U;
    test_save_status = MT_SD_OK;
    test_effect_calls = 0U;
    test_display_enabled=test_display_fail=test_display_timeout=false;
    test_display_status=test_display_last_request=test_clock_ms=0U;
    test_display_writes=test_display_invalidations=0U;
    test_effect_available = true;
    mt_video_effects_defaults(&test_effects);
    settings_faulted = false;
    supervisor_menu_dirty = false;
    mt_supervisor_settings_defaults(&persistent_settings);
}

static void assert_unchanged(const mt_supervisor_settings_t *before)
{
    assert(test_mmio_writes == 0U && test_border == UINT32_C(0x670));
    assert(test_vectrex_video == UINT32_C(0xB));
    assert(memcmp(before, &persistent_settings, sizeof(*before)) == 0);
    assert(supervisor_status_color == MT_UI_COLOR_RED);
}

static void test_guards(void)
{
    static const uint32_t invalid_borders[] = {
        UINT32_C(0x2000), UINT32_C(0x67b), UINT32_C(0x71), UINT32_C(0x1571)
    };
    mt_supervisor_settings_t before;
    size_t index;
    test_reset(MT_VECTREX_CORE_ID, "vectrex");
    before = persistent_settings;
    test_abi = 11U;
    change_active_border(UINT32_C(0x671));
    assert_unchanged(&before);
    assert(test_save_calls == 0U);
    test_abi = UINT32_C(0x1000C);
    change_active_border(UINT32_C(0x671));
    assert_unchanged(&before);
    test_abi = MT_ABI_VERSION;
    test_core_id = MT_GBC_CORE_ID;
    change_active_border(UINT32_C(0x671));
    assert_unchanged(&before);
    test_core_id = MT_VECTREX_CORE_ID;
    supervisor.has_active = false;
    change_active_border(UINT32_C(0x671));
    assert_unchanged(&before);
    supervisor.has_active = true;
    for (index = 0U; index < sizeof(invalid_borders) / sizeof(invalid_borders[0]); ++index) {
        change_active_border(invalid_borders[index]);
        assert_unchanged(&before);
    }
    if (SIZE_MAX > UINT32_MAX) {
        change_active_border((size_t)UINT32_MAX + 1U);
        assert_unchanged(&before);
    }
    assert_unchanged(&before);
    assert(test_save_calls == 0U);
}

static void test_apply_and_failure(void)
{
    static const mt_sd_status_t failures[] = {
        MT_SD_OPEN_FAILED, MT_SD_WRITE_FAILED, MT_SD_SYNC_FAILED,
        MT_SD_CLOSE_FAILED, MT_SD_UNLINK_FAILED
    };
    static const uint32_t cores[] = {MT_C64_CORE_ID, MT_GBC_CORE_ID, MT_VECTREX_CORE_ID};
    static const char *const ids[] = {"c64", "gbc", "vectrex"};
    size_t index;
    size_t core;
    for (core = 0U; core < 3U; ++core) {
        test_reset(cores[core], ids[core]);
        change_active_border(UINT32_C(0x14f9));
        assert(test_mmio_writes == 1U && test_save_calls == 1U);
        assert(test_border == UINT32_C(0x14f9));
        assert(mt_supervisor_settings_find(&persistent_settings, ids[core])->border_enabled);
    }
    for (index = 0U; index < sizeof(failures) / sizeof(failures[0]); ++index) {
        mt_supervisor_settings_t before;
        test_reset(MT_VECTREX_CORE_ID, "vectrex");
        assert(mt_supervisor_settings_remember_core(&persistent_settings, "c64", "pal"));
        persistent_settings.auto_boot = true;
        before = persistent_settings;
        test_save_status = failures[index];
        change_active_border(UINT32_C(0x671));
        assert_unchanged(&before);
        assert(settings_faulted && test_save_calls == 1U);
        change_active_border(UINT32_C(0x14f9));
        assert_unchanged(&before);
        assert(settings_faulted && test_save_calls == 2U);
        test_save_status = MT_SD_OK;
        change_active_border(UINT32_C(0x671));
        assert(!settings_faulted && test_mmio_writes == 1U && test_border == UINT32_C(0x671));
        assert(persistent_settings.auto_boot && strcmp(persistent_settings.selected_core_id, "c64") == 0);
    }
}

static void test_restore_gate(void)
{
    mt_core_selection_t next;
    const mt_core_settings_profile_t *profile;
    test_reset(MT_C64_CORE_ID, "c64");
    next = supervisor.active;
    next.package.core_id = MT_VECTREX_CORE_ID;
    strcpy(next.package.id, "vectrex");
    /* Activation calls restore before supervisor.active has been replaced.
     * The live shell identity, not the old active selection, is authoritative. */
    assert(!selection_supports_borders(&next));
    assert(restore_selection_settings(&next));
    assert(test_mmio_writes == 0U);
    test_core_id = MT_VECTREX_CORE_ID;
    assert(selection_supports_borders(&next));
    assert(!selection_supports_borders(&supervisor.active));
    supervisor.has_active = false;
    assert(selection_supports_borders(&next));
    assert(restore_selection_settings(&next));
    assert(test_mmio_writes == 2U && test_border == UINT32_C(0x670));
    assert(test_vectrex_video == UINT32_C(0xB));
    test_mmio_writes = 0U;
    assert(profile_border_config(NULL) == UINT32_C(0x670));
    assert(mt_supervisor_settings_remember_variant(&persistent_settings, "vectrex", "default"));
    assert(mt_supervisor_settings_remember_borders(&persistent_settings, "vectrex", true, 4U, 15U, 20U));
    profile = mt_supervisor_settings_find(&persistent_settings, "vectrex");
    assert(profile_border_config(profile) == UINT32_C(0x14f9));
    assert(restore_selection_settings(&next));
    assert(test_mmio_writes == 2U && test_border == UINT32_C(0x14f9));
    test_mmio_writes = 0U;
    test_abi = 11U;
    assert(!selection_supports_borders(&next));
    assert(restore_selection_settings(&next));
    assert(test_save_calls == 0U && test_mmio_writes == 0U);
}

static void test_vectrex_video_guards_and_saves(void)
{
    static const mt_sd_status_t failures[] = {
        MT_SD_OPEN_FAILED, MT_SD_WRITE_FAILED, MT_SD_SYNC_FAILED,
        MT_SD_CLOSE_FAILED, MT_SD_UNLINK_FAILED
    };
    mt_supervisor_settings_t before;
    size_t index;
    test_reset(MT_VECTREX_CORE_ID, "vectrex");
    before = persistent_settings;
    test_abi = 12U;
    change_active_vectrex_video(UINT32_C(0x1F));
    assert_unchanged(&before);
    test_abi = UINT32_C(0x1000D);
    change_active_vectrex_video(UINT32_C(0x1F));
    assert_unchanged(&before);
    test_abi = MT_ABI_VERSION; test_core_id = MT_GBC_CORE_ID;
    change_active_vectrex_video(UINT32_C(0x1F));
    assert_unchanged(&before);
    test_core_id = MT_VECTREX_CORE_ID; supervisor.has_active = false;
    change_active_vectrex_video(UINT32_C(0x1F));
    assert_unchanged(&before);
    supervisor.has_active = true;
    change_active_vectrex_video(UINT32_C(0x20));
    assert_unchanged(&before);
    if (SIZE_MAX > UINT32_MAX) {
        change_active_vectrex_video((size_t)UINT32_MAX + 1U);
        assert_unchanged(&before);
    }
    assert(test_save_calls == 0U);
    test_reset(MT_C64_CORE_ID, "c64"); before = persistent_settings;
    change_active_vectrex_video(UINT32_C(0x1F));
    assert_unchanged(&before);
    assert(test_save_calls == 0U);
    for (index = 0U; index < 32U; ++index) {
        const mt_core_settings_profile_t *profile;
        test_reset(MT_VECTREX_CORE_ID, "vectrex");
        change_active_vectrex_video(index);
        assert(test_mmio_writes == 1U && test_save_calls == 1U);
        assert(test_vectrex_video == index && test_border == UINT32_C(0x670));
        profile = mt_supervisor_settings_find(&persistent_settings, "vectrex");
        assert(profile != NULL && profile_vectrex_video_config(profile) == index);
    }
    for (index = 0U; index < sizeof(failures) / sizeof(failures[0]); ++index) {
        test_reset(MT_VECTREX_CORE_ID, "vectrex");
        assert(mt_supervisor_settings_remember_core(&persistent_settings, "c64", "pal"));
        persistent_settings.auto_boot = true; before = persistent_settings;
        test_save_status = failures[index];
        change_active_vectrex_video(UINT32_C(0x1F));
        assert_unchanged(&before);
        assert(settings_faulted && test_save_calls == 1U);
        test_save_status = MT_SD_OK;
        change_active_vectrex_video(UINT32_C(0x1F));
        assert(!settings_faulted && test_save_calls == 2U && test_mmio_writes == 1U);
        assert(test_vectrex_video == UINT32_C(0x1F) && persistent_settings.auto_boot);
        assert(strcmp(persistent_settings.selected_core_id, "c64") == 0);
    }
}

static void test_vectrex_video_restore(void)
{
    mt_core_selection_t next;
    mt_core_settings_profile_t invalid = {0};
    test_reset(MT_C64_CORE_ID, "c64");
    next = supervisor.active; next.package.core_id = MT_VECTREX_CORE_ID;
    strcpy(next.package.id, "vectrex");
    assert(!selection_supports_vectrex_video(NULL));
    assert(!selection_supports_vectrex_video(&next));
    assert(restore_selection_settings(&next) && test_mmio_writes == 0U);
    test_core_id = MT_VECTREX_CORE_ID;
    assert(selection_supports_vectrex_video(&next));
    assert(!selection_supports_vectrex_video(&supervisor.active));
    assert(mt_supervisor_settings_remember_variant(&persistent_settings, "vectrex", "default"));
    assert(mt_supervisor_settings_remember_vectrex_video(&persistent_settings, "vectrex", false, 3U, 2U));
    assert(restore_selection_settings(&next));
    assert(test_mmio_writes == 2U && test_vectrex_video == UINT32_C(0x16));
    test_mmio_writes = 0U; test_vectrex_video = UINT32_C(0x1F); test_abi = 12U;
    assert(!selection_supports_vectrex_video(&next));
    assert(restore_selection_settings(&next));
    assert(test_mmio_writes == 1U && test_vectrex_video == UINT32_C(0x1F));
    assert(test_save_calls == 0U);
    assert(profile_vectrex_video_config(NULL) == UINT32_C(0xB));
    invalid.vectrex_overlay_enabled = true;
    invalid.vectrex_beam_gain = invalid.vectrex_overlay_opacity = UINT8_MAX;
    assert(profile_vectrex_video_config(&invalid) == UINT32_C(0xB));
}

static void test_effect_handlers(void)
{
    test_reset(MT_GBC_CORE_ID, "gbc");
    assert(selection_supports_effects(&supervisor.active));
    /* Older cores interpret the same fields as output filtering and 2-D blur. */
    test_abi = 16U;
    assert(!selection_supports_effects(&supervisor.active));
    test_abi = MT_ABI_VERSION;
    supervisor.active.package.abi_minor = 16U;
    assert(!selection_supports_effects(&supervisor.active));
    supervisor.active.package.abi_minor = MT_ABI_MINOR;
    change_active_video_effect(MT_VIDEO_EFFECT_ENABLED, 2U);
    assert(test_effect_calls == 1U && test_save_calls == 1U && test_effects.enabled == 1U);
    assert(test_mmio_writes == 0U); /* Applying is asynchronous. */
    change_active_video_effect(MT_VIDEO_EFFECT_BLOOM, 2U);
    assert(test_effect_calls == 2U && test_effects.bloom == 1U);
    const mt_supervisor_settings_t before = persistent_settings;
    test_save_status = MT_SD_WRITE_FAILED;
    change_active_video_effect(MT_VIDEO_EFFECT_BLOOM, 2U);
    assert(test_effect_calls == 2U && test_effects.bloom == 1U);
    assert(memcmp(&before, &persistent_settings, sizeof(before)) == 0);
    test_abi = 13U;
    assert(!selection_supports_effects(&supervisor.active));
    change_active_video_effect(MT_VIDEO_EFFECT_ENABLED, 1U);
    assert(test_effect_calls == 2U);
    test_abi = MT_ABI_VERSION; test_effect_available = false;
    change_active_video_effect(MT_VIDEO_EFFECT_ENABLED, 1U);
    assert(test_effect_calls == 2U);
}

static void test_multiplier_runtime(void)
{
    test_reset(MT_GBC_CORE_ID,"gbc");
    test_display_enabled=true;test_display_status=3U;
    assert(selection_supports_video_multiplier(&supervisor.active));
    change_active_video_multiplier(4U);
    assert(test_display_writes==1U && test_save_calls==1U && test_display_status==67U);
    assert(test_display_invalidations==1U && supervisor_status_color!=MT_UI_COLOR_RED);
    const mt_core_settings_profile_t *profile=mt_supervisor_settings_find(&persistent_settings,"gbc");
    assert(profile!=NULL && profile->video_multiplier==4U);
    const mt_supervisor_settings_t before=persistent_settings;
    test_save_status=MT_SD_WRITE_FAILED;
    change_active_video_multiplier(2U);
    assert(test_display_writes==3U && test_display_status==67U && test_save_calls==2U);
    assert(memcmp(&persistent_settings,&before,sizeof(before))==0);
    test_save_status=MT_SD_OK;test_display_fail=true;
    change_active_video_multiplier(1U);
    assert(test_save_calls==2U && memcmp(&persistent_settings,&before,sizeof(before))==0);
    test_display_fail=false;test_display_status=67U;test_display_timeout=true;
    change_active_video_multiplier(1U);
    assert(test_save_calls==2U && memcmp(&persistent_settings,&before,sizeof(before))==0 && test_clock_ms<4000U);
    test_display_timeout=false;test_display_status=67U;
    const unsigned writes_before=test_display_writes;
    change_active_video_multiplier(5U);
    assert(test_display_writes==writes_before && test_save_calls==2U);
    test_abi=20U;change_active_video_multiplier(2U);
    assert(test_display_writes==writes_before && test_save_calls==2U);
    assert(persistent_settings.profiles[0].video_multiplier==4U);
    test_abi=MT_ABI_VERSION;supervisor.active.package.abi_minor=20U;
    change_active_video_multiplier(2U);assert(test_display_writes==writes_before);
    supervisor.active.package.abi_minor=MT_ABI_MINOR;
    test_display_status=0U;persistent_settings.display_mode=2U;
    test_core_id=MT_VECTREX_CORE_ID;supervisor.active.package.core_id=MT_VECTREX_CORE_ID;
    strcpy(supervisor.active.package.id,"vectrex");
    assert(mt_supervisor_settings_remember_variant(&persistent_settings,"vectrex","default"));
    assert(mt_supervisor_settings_remember_video_multiplier(&persistent_settings,"vectrex",4U));
    assert(restore_selection_settings(&supervisor.active));
    assert(test_display_status==66U && test_display_last_request==66U);
    mt_core_selection_t new_core=supervisor.active;
    strcpy(new_core.package.id,"vectrex-new");
    assert(restore_selection_settings(&new_core) && test_display_status==2U);
    assert(restore_selection_settings(&supervisor.active) && test_display_status==66U);
    /* A different core without a saved size receives Auto at its own restore. */
    test_core_id=MT_GENESIS_CORE_ID;supervisor.active.package.core_id=MT_GENESIS_CORE_ID;
    strcpy(supervisor.active.package.id,"genesis");
    /* Genesis has media/pad restore dependencies; exercise only its geometry
     * selection here, while the complete Vectrex restore above uses production code. */
    profile=mt_supervisor_settings_find(&persistent_settings,"genesis");
    assert(profile==NULL && apply_video_multiplier(profile!=NULL ? profile->video_multiplier:0U));
    assert(test_display_status==2U);
}

int main(void)
{
    test_guards();
    test_apply_and_failure();
    test_restore_gate();
    test_vectrex_video_guards_and_saves();
    test_vectrex_video_restore();
    test_effect_handlers();
    test_multiplier_runtime();
    puts("PASS: board border/Vectrex video guards, save ordering, rollback and restore selection");
    return 0;
}
