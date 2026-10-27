/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_drive_activity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Both layouts address the same two text memories: legacy cell i is dense
 * cell i. The fake keeps one cell array per bank to model that aliasing. */
typedef struct {
    uint32_t status;
    uint32_t pending_commit;
    bool commit_pending;
    unsigned dense_writes, legacy_writes, commits;
    uint16_t cells[2][MT_UI_COLUMNS * MT_UI_ROWS];
} fake_overlay_t;

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static uint32_t read32(void *context, uint32_t offset)
{
    fake_overlay_t *const overlay = context;
    check(offset == MT_REG_UI_STATUS, "only UI status is read");
    return overlay->status;
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    fake_overlay_t *const overlay = context;
    const size_t active = (overlay->status & MT_UI_STATUS_BANK) != 0U ? 1U : 0U;
    size_t bank, cell;
    if (offset == MT_REG_UI_COMMIT) {
        check(!overlay->commit_pending, "commit never overwrites a pending commit");
        overlay->commit_pending = true;
        overlay->pending_commit = value;
        ++overlay->commits;
        return;
    }
    if (offset >= MT_UI_VRAM_BANK0_OFFSET) {
        bank = offset >= MT_UI_VRAM_BANK1_OFFSET ? 1U : 0U;
        cell = (offset - (bank != 0U ? MT_UI_VRAM_BANK1_OFFSET : MT_UI_VRAM_BANK0_OFFSET)) / 2U;
        check(cell + 1U < MT_UI_COLUMNS * MT_UI_ROWS, "dense write in range");
        ++overlay->dense_writes;
    } else {
        bank = offset >= MT_UI_LEGACY_VRAM_BANK1_OFFSET ? 1U : 0U;
        cell = (offset - (bank != 0U ? MT_UI_LEGACY_VRAM_BANK1_OFFSET :
                                       MT_UI_LEGACY_VRAM_BANK0_OFFSET)) / 2U;
        check(offset >= MT_UI_LEGACY_VRAM_BANK0_OFFSET &&
              cell + 1U < MT_UI_LEGACY_COLUMNS * MT_UI_LEGACY_ROWS, "legacy write in range");
        ++overlay->legacy_writes;
    }
    check(bank != active, "uploads never touch the displayed bank");
    overlay->cells[bank][cell] = (uint16_t)value;
    overlay->cells[bank][cell + 1U] = (uint16_t)(value >> 16U);
}

static void vblank(fake_overlay_t *overlay)
{
    check(overlay->commit_pending, "vertical blank requires a pending commit");
    overlay->status = MT_UI_STATUS_READY | MT_UI_STATUS_DENSE_LAYOUT;
    if (overlay->pending_commit & MT_UI_COMMIT_DENSE) overlay->status |= MT_UI_STATUS_DENSE;
    if (overlay->pending_commit & MT_UI_COMMIT_BANK) overlay->status |= MT_UI_STATUS_BANK;
    if (overlay->pending_commit & MT_UI_COMMIT_VISIBLE) overlay->status |= MT_UI_STATUS_VISIBLE;
    overlay->commit_pending = false;
    overlay->dense_writes = overlay->legacy_writes = overlay->commits = 0U;
}

static bool displays_dense(const fake_overlay_t *overlay, const mt_ui_frame_t *frame)
{
    const size_t bank = (overlay->status & MT_UI_STATUS_BANK) != 0U ? 1U : 0U;
    return memcmp(overlay->cells[bank], frame->cells, sizeof(frame->cells)) == 0;
}

static bool displays_legacy(const fake_overlay_t *overlay, const mt_ui_legacy_frame_t *frame)
{
    const size_t bank = (overlay->status & MT_UI_STATUS_BANK) != 0U ? 1U : 0U;
    return memcmp(overlay->cells[bank], frame->cells, sizeof(frame->cells)) == 0;
}

static char badge_char(const mt_ui_legacy_frame_t *frame, size_t column)
{
    return (char)(frame->cells[MT_UI_LEGACY_COLUMNS - MT_ACTIVITY_BADGE_WIDTH + column] & 0x7fU);
}

static unsigned badge_background(const mt_ui_legacy_frame_t *frame, size_t column)
{
    return (frame->cells[MT_UI_LEGACY_COLUMNS - MT_ACTIVITY_BADGE_WIDTH + column] >> 11U) & 15U;
}

static void test_model(void)
{
    static mt_activity_t activity;
    static mt_ui_legacy_frame_t frame;
    const uint16_t clear = mt_ui_cell(' ', MT_UI_COLOR_TEXT, MT_UI_COLOR_BACKGROUND, true);

    mt_activity_reset(&activity);
    check(!mt_activity_render(&activity, 1000U, &frame), "a fresh model is hidden");
    for (size_t cell = 0U; cell < MT_ACTIVITY_CELLS; ++cell)
        check(frame.cells[cell] == clear, "a hidden frame is fully transparent");

    /* Existing totals are recorded, not shown. */
    mt_activity_sample(&activity, 0U, "D8", 40U, 3U, 1000U);
    mt_activity_sample(&activity, 1U, "D9", 7U, 7U, 1000U);
    check(!mt_activity_render(&activity, 1001U, &frame), "first samples do not flash");

    /* A read lights R and names the unit; the badge outlives the light. */
    mt_activity_sample(&activity, 1U, "D9", 8U, 7U, 2000U);
    check(mt_activity_render(&activity, 2000U, &frame), "a read shows the badge");
    check(badge_char(&frame, 1U) == 'D' && badge_char(&frame, 2U) == '9' &&
          badge_char(&frame, 7U) == 'R' && badge_char(&frame, 8U) == 'W',
          "the badge names the unit and its lights");
    check(badge_background(&frame, 7U) == MT_UI_COLOR_SUCCESS &&
          badge_background(&frame, 8U) == MT_UI_COLOR_PANEL &&
          badge_background(&frame, 6U) == MT_UI_COLOR_RED,
          "a unit without its own LED lights it on activity");
    check(frame.cells[0] == clear && frame.cells[MT_UI_LEGACY_COLUMNS] == clear,
          "only the top-right badge is opaque");
    check(mt_activity_render(&activity, 2000U + MT_ACTIVITY_READ_HOLD_MS, &frame) &&
          badge_background(&frame, 7U) == MT_UI_COLOR_PANEL &&
          badge_background(&frame, 6U) == MT_UI_COLOR_DIM,
          "the read light holds for its interval only");
    check(!mt_activity_render(&activity, 2000U + MT_ACTIVITY_VISIBLE_HOLD_MS, &frame),
          "the badge hides after the visible hold");

    /* A write holds longer and moves the badge to the newest unit. */
    mt_activity_sample(&activity, 0U, "D8", 40U, 4U, 5000U);
    check(mt_activity_render(&activity, 5000U + MT_ACTIVITY_READ_HOLD_MS, &frame) &&
          badge_char(&frame, 2U) == '8' && badge_background(&frame, 8U) == MT_UI_COLOR_WARNING,
          "a write keeps W lit beyond the read hold");

    /* A live LED shows its own state, and a lit LED keeps the badge visible. */
    mt_activity_lights(&activity, 0U, "D8", true, true, 9000U);
    check(mt_activity_render(&activity, 9000U, &frame) &&
          badge_background(&frame, 6U) == MT_UI_COLOR_RED, "a lit drive LED shows red");
    mt_activity_lights(&activity, 0U, "D8", true, false, 20000U);
    check(mt_activity_render(&activity, 20000U, &frame), "a lit LED keeps the badge visible");
    /* A steady LED does not take the badge from a newer event elsewhere. */
    mt_activity_sample(&activity, 1U, "D9", 9U, 7U, 20001U);
    mt_activity_lights(&activity, 0U, "D8", true, false, 20002U);
    check(mt_activity_render(&activity, 20002U, &frame) && badge_char(&frame, 2U) == '9',
          "only a rising light selects its unit");
    mt_activity_lights(&activity, 0U, "D8", false, false, 20003U);
    check(mt_activity_render(&activity, 20003U, &frame) &&
          badge_background(&frame, 6U) == MT_UI_COLOR_RED,
          "D9 shows its own activity light");
    check(!mt_activity_render(&activity, 20003U + MT_ACTIVITY_VISIBLE_HOLD_MS, &frame),
          "all units idle hides the badge");
    mt_activity_lights(&activity, 0U, "D8", true, false, 30000U);
    check(mt_activity_render(&activity, 30000U, &frame) && badge_char(&frame, 2U) == '8',
          "a rising LED selects its unit again");

    /* Out-of-range units and long labels are bounded. */
    mt_activity_sample(&activity, MT_ACTIVITY_MAX_UNITS, "X", 1U, 1U, 30001U);
    mt_activity_sample(&activity, 3U, "LONGLABEL", 0U, 0U, 30001U);
    mt_activity_sample(&activity, 3U, "LONGLABEL", 0U, 1U, 30002U);
    check(mt_activity_render(&activity, 30002U, &frame) && badge_char(&frame, 4U) == 'G' &&
          badge_char(&frame, 5U) == ' ', "labels clip to four cells");
    mt_activity_reset(&activity);
    check(!mt_activity_render(&activity, 30002U, &frame), "reset clears every unit");
}

static void test_shared_presenter(void)
{
    static fake_overlay_t overlay;
    static mt_ui_presenter_t presenter;
    static mt_ui_frame_t menu;
    static mt_ui_legacy_frame_t badge;
    const mt_ui_bus_t bus = {&overlay, read32, write32};
    const unsigned legacy_words = MT_UI_LEGACY_COLUMNS * MT_UI_LEGACY_ROWS / 2U;

    overlay.status = MT_UI_STATUS_READY | MT_UI_STATUS_DENSE_LAYOUT;
    mt_ui_presenter_init(&presenter);
    mt_ui_clear(&menu, MT_UI_COLOR_PANEL);
    mt_ui_text(&menu, 2U, 1U, "Menu", MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL, false);
    for (size_t cell = 0U; cell < MT_ACTIVITY_CELLS; ++cell)
        badge.cells[cell] = mt_ui_cell(' ', MT_UI_COLOR_TEXT, MT_UI_COLOR_BACKGROUND, true);
    badge.cells[75] = mt_ui_cell('D', MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL, false);

    /* The menu shows and pauses; closing it hides the dense frame. */
    check(mt_ui_present_cached(&presenter, &bus, &menu, true) == MT_UI_PRESENT_OK, "menu shows");
    vblank(&overlay);
    check(mt_ui_status_menu_shown(overlay.status) && displays_dense(&overlay, &menu),
          "a visible dense frame pauses");
    check(mt_ui_present_cached(&presenter, &bus, &menu, false) == MT_UI_PRESENT_OK, "menu hides");
    vblank(&overlay);
    check(!mt_ui_status_menu_shown(overlay.status), "a hidden menu does not pause");

    /* The badge uploads the legacy layout and shows without pausing. */
    check(mt_ui_present_legacy_cached(&presenter, &bus, &badge, true) == MT_UI_PRESENT_OK &&
          overlay.legacy_writes == legacy_words && overlay.dense_writes == 0U &&
          overlay.pending_commit == MT_UI_COMMIT_VISIBLE,
          "the badge commits a visible legacy bank");
    check(mt_ui_present_legacy_cached(&presenter, &bus, &badge, true) == MT_UI_PRESENT_BUSY,
          "a pending commit is not overwritten");
    vblank(&overlay);
    check((overlay.status & MT_UI_STATUS_VISIBLE) && !mt_ui_status_menu_shown(overlay.status) &&
          displays_legacy(&overlay, &badge), "a visible legacy frame does not pause");
    check(mt_ui_present_legacy_cached(&presenter, &bus, &badge, true) == MT_UI_PRESENT_OK &&
          overlay.commits == 0U, "an unchanged badge needs no writes");

    /* A one-cell change uploads one word into the other bank. */
    badge.cells[78] = mt_ui_cell('R', MT_UI_COLOR_BACKGROUND, MT_UI_COLOR_SUCCESS, false);
    check(mt_ui_present_legacy_cached(&presenter, &bus, &badge, true) == MT_UI_PRESENT_OK &&
          overlay.legacy_writes == legacy_words && overlay.commits == 1U,
          "the first upload to the other bank is complete");
    vblank(&overlay);
    badge.cells[78] = mt_ui_cell('R', MT_UI_COLOR_DIM, MT_UI_COLOR_PANEL, false);
    check(mt_ui_present_legacy_cached(&presenter, &bus, &badge, true) == MT_UI_PRESENT_OK &&
          overlay.legacy_writes == 1U, "later changes upload only changed words");
    vblank(&overlay);
    check(displays_legacy(&overlay, &badge), "the displayed bank holds the badge");

    /* Opening the menu again must rewrite what the badge overwrote. */
    check(mt_ui_present_cached(&presenter, &bus, &menu, true) == MT_UI_PRESENT_OK &&
          overlay.dense_writes == MT_UI_COLUMNS * MT_UI_ROWS / 2U,
          "a badge upload invalidates that bank's menu cache");
    vblank(&overlay);
    check(mt_ui_status_menu_shown(overlay.status) && displays_dense(&overlay, &menu),
          "the menu replaces the badge");

    /* Bank 0 still holds the badge: hiding it there needs only a commit. */
    check(mt_ui_present_legacy_cached(&presenter, &bus, &badge, false) == MT_UI_PRESENT_OK &&
          overlay.legacy_writes == 0U && overlay.pending_commit == 0U,
          "an intact badge bank needs no upload");
    vblank(&overlay);
    check(!(overlay.status & MT_UI_STATUS_VISIBLE) && !(overlay.status & MT_UI_STATUS_DENSE),
          "a hidden badge leaves the overlay hidden");
    check(mt_ui_present_cached(&presenter, &bus, &menu, true) == MT_UI_PRESENT_OK &&
          overlay.dense_writes == 0U, "bank 1 still holds the menu");
    vblank(&overlay);
    mt_ui_text(&menu, 2U, 2U, "Changed", MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL, false);
    check(mt_ui_present_cached(&presenter, &bus, &menu, true) == MT_UI_PRESENT_OK &&
          overlay.dense_writes == MT_UI_COLUMNS * MT_UI_ROWS / 2U,
          "the badge's bank gets the complete menu");
    vblank(&overlay);
    /* And the badge must rewrite what the menu overwrote. */
    check(mt_ui_present_legacy_cached(&presenter, &bus, &badge, true) == MT_UI_PRESENT_OK &&
          overlay.legacy_writes == legacy_words,
          "a menu upload invalidates that bank's badge cache");
    vblank(&overlay);
    check(displays_legacy(&overlay, &badge) && !mt_ui_status_menu_shown(overlay.status),
          "the badge returns after the menu closes");
    check(mt_ui_status_menu_shown(MT_UI_STATUS_VISIBLE) &&
          !mt_ui_status_menu_shown(MT_UI_STATUS_VISIBLE | MT_UI_STATUS_DENSE_LAYOUT) &&
          mt_ui_status_menu_shown(MT_UI_STATUS_VISIBLE | MT_UI_STATUS_DENSE_LAYOUT |
                                  MT_UI_STATUS_DENSE),
          "only a dense-capable overlay shows a legacy frame as the badge");
    mt_ui_presenter_invalidate(&presenter);
    check(!presenter.legacy_valid[0] && !presenter.legacy_valid[1] &&
          !presenter.bank_valid[0] && !presenter.bank_valid[1], "invalidate clears both layouts");
    overlay.status = MT_UI_STATUS_READY;
    check(mt_ui_present_legacy_cached(&presenter, &bus, &badge, true) ==
          MT_UI_PRESENT_UNSUPPORTED_LAYOUT, "pre-0.11 overlays are refused");
    check(mt_ui_present_legacy_cached(NULL, &bus, &badge, true) == MT_UI_PRESENT_INVALID_ARGUMENT &&
          mt_ui_present_legacy_cached(&presenter, &bus, NULL, true) == MT_UI_PRESENT_INVALID_ARGUMENT,
          "missing arguments are refused");
}

int main(void)
{
    test_model();
    test_shared_presenter();
    puts("PASS drive activity model, legacy badge presenter and shared bank caches");
    return 0;
}
