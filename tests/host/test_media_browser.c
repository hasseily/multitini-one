/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_media_browser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void test_navigation(void)
{
    mt_media_browser_t browser;
    char path[512];
    check(mt_media_browser_init(&browser, "0:/multitini/cores/vectrex/"), "initialize media root");
    check(browser.relative[0] == '\0' && !mt_media_browser_can_up(&browser), "root has no parent");
    check(!mt_media_browser_up(&browser), "up cannot escape root");
    check(mt_media_browser_path(&browser, path, sizeof(path)) &&
          strcmp(path, "0:/multitini/cores/vectrex") == 0, "normalize trailing root slash");
    check(mt_media_browser_resolve(&browser, "Mine Storm.vec", path, sizeof(path)) &&
          strcmp(path, "Mine Storm.vec") == 0, "root file resolves relatively");
    check(mt_media_browser_enter(&browser, "Games") &&
          mt_media_browser_enter(&browser, "Vecalabeth"), "enter nested folders");
    check(mt_media_browser_can_up(&browser), "nested directory has parent");
    check(mt_media_browser_resolve(&browser, "start.bas", path, sizeof(path)) &&
          strcmp(path, "Games/Vecalabeth/start.bas") == 0, "nested file retains its relative directory");
    check(mt_media_browser_path(&browser, path, sizeof(path)) &&
          strcmp(path, "0:/multitini/cores/vectrex/Games/Vecalabeth") == 0, "current directory remains rooted");
    check(mt_media_browser_up(&browser) && strcmp(browser.relative, "Games") == 0,
          "up removes exactly one component");
    check(mt_media_browser_up(&browser) && !mt_media_browser_up(&browser), "up stops at root");
    check(mt_media_browser_init(&browser, "/") && mt_media_browser_enter(&browser, "Games") &&
          mt_media_browser_path(&browser, path, sizeof(path)) && strcmp(path, "/Games") == 0,
          "absolute root does not produce a double slash");
    check(mt_media_browser_init(&browser, "0:/") && mt_media_browser_enter(&browser, "Games") &&
          mt_media_browser_path(&browser, path, sizeof(path)) && strcmp(path, "0:/Games") == 0,
          "volume root does not produce a double slash");
    check(mt_media_browser_init(&browser, "media") && mt_media_browser_enter(&browser, "ROMs") &&
          mt_media_browser_path(&browser, path, sizeof(path)) && strcmp(path, "media/ROMs") == 0,
          "portable relative root");
}

static void test_rejection(void)
{
    static const char *const bad[] = {"", ".", "..", "../escape", "a/b", "a\\b",
        "0:/other", "bad:name", "wild*", "wild?", "quote\"", "<name", ">name", "a|b",
        "control\001", "delete\177", "trailing.", "trailing "};
    static const char *const bad_roots[] = {"", "../media", "media/../escape", "media/./games",
        "0:/media//games", "0:media", "media\\games"};
    mt_media_browser_t browser, before;
    char output[32] = "unchanged";
    size_t index;
    check(mt_media_browser_init(&browser, "0:/media") && mt_media_browser_enter(&browser, "Games"),
          "initialize rejection fixture");
    before = browser;
    for (index = 0U; index < sizeof(bad) / sizeof(bad[0]); ++index) {
        check(!mt_media_browser_valid_component(bad[index]), "reject unsafe component");
        check(!mt_media_browser_enter(&browser, bad[index]) &&
              !mt_media_browser_resolve(&browser, bad[index], output, sizeof(output)), "reject unsafe child");
        check(memcmp(&browser, &before, sizeof(browser)) == 0 && strcmp(output, "unchanged") == 0,
              "rejected child preserves state and output");
    }
    for (index = 0U; index < sizeof(bad_roots) / sizeof(bad_roots[0]); ++index)
        check(!mt_media_browser_init(&browser, bad_roots[index]) &&
              memcmp(&browser, &before, sizeof(browser)) == 0, "rejected root preserves state");
    check(mt_media_browser_valid_component("game..bas") &&
          mt_media_browser_valid_component("Dungeon 1") &&
          mt_media_browser_valid_component("caf\303\251.bas"), "safe dotted, spaced and UTF-8 names");
    check(!mt_media_browser_init(NULL, "media") && !mt_media_browser_init(&browser, NULL) &&
          !mt_media_browser_valid_component(NULL) && !mt_media_browser_enter(NULL, "Games") &&
          !mt_media_browser_up(NULL) && !mt_media_browser_can_up(NULL), "null API arguments");
    strcpy(browser.relative, "../escape");
    check(!mt_media_browser_path(&browser, output, sizeof(output)) && strcmp(output, "unchanged") == 0,
          "invalid public state cannot escape root");
}

static void test_lengths(void)
{
    mt_media_browser_t browser, before;
    char component[256], too_long[257], path[512], output[32] = "unchanged";
    memset(component, 'A', 255U); component[255] = '\0';
    memset(too_long, 'A', 256U); too_long[256] = '\0';
    check(mt_media_browser_init(&browser, component) && mt_media_browser_enter(&browser, component),
          "maximum root and relative length accepted");
    check(mt_media_browser_path(&browser, path, sizeof(path)) && strlen(path) == 511U,
          "maximum rooted path includes separator and terminator");
    before = browser;
    check(!mt_media_browser_enter(&browser, "x") && memcmp(&browser, &before, sizeof(browser)) == 0,
          "nested overlength directory is rejected atomically");
    check(!mt_media_browser_path(&browser, output, sizeof(output)) && strcmp(output, "unchanged") == 0,
          "small rooted output buffer is untouched");
    check(!mt_media_browser_init(&browser, too_long) && !mt_media_browser_valid_component(too_long),
          "overlength strings rejected");
    check(mt_media_browser_init(&browser, "media"), "reset length fixture");
    check(!mt_media_browser_resolve(&browser, "game.bas", output, 8U) && strcmp(output, "unchanged") == 0,
          "relative output requires space for terminator");
    check(mt_media_browser_resolve(&browser, "game.bas", output, 9U) && strcmp(output, "game.bas") == 0,
          "exact output capacity accepted");
    component[127] = '\0';
    check(mt_media_browser_enter(&browser, component) && mt_media_browser_enter(&browser, component) &&
          strlen(browser.relative) == 255U, "nested components reach exact relative limit");
    check(mt_media_browser_up(&browser) && strlen(browser.relative) == 127U,
          "up remains available at length limit");
}

static void test_relative_join(void)
{
    static const char *const bad[] = {"", ".", "..", "../escape.prg",
        "Games/../escape.prg", "Games/./x.prg", "Games//x.prg", "/x.prg",
        "0:/x.prg", "Games\\x.prg", "Games/x.prg/", "Games./x.prg",
        "Games /x.prg", "Games/wild*.prg", "Games/x.prg:stream"};
    char output[512], maximum[256], too_long[257];
    size_t i;
    check(mt_media_browser_join("0:/library/c64/", "Games/Demos/demo.prg", output, sizeof(output)) &&
          strcmp(output, "0:/library/c64/Games/Demos/demo.prg") == 0, "join nested package path");
    for (i = 0U; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        strcpy(output, "unchanged");
        check(!mt_media_browser_join("0:/library/c64", bad[i], output, sizeof(output)) &&
              strcmp(output, "unchanged") == 0, "relative join rejects escape and preserves output");
    }
    memset(maximum, 'a', 255U); maximum[255] = '\0';
    memset(too_long, 'a', 256U); too_long[256] = '\0';
    check(mt_media_browser_join(maximum, maximum, output, sizeof(output)) && strlen(output) == 511U,
          "join maximum root and relative path");
    check(!mt_media_browser_join(maximum, maximum, output, sizeof(output) - 1U) &&
          !mt_media_browser_join(maximum, too_long, output, sizeof(output)) &&
          !mt_media_browser_join("0:/a/../b", "x.prg", output, sizeof(output)) &&
          !mt_media_browser_join("0:/a", NULL, output, sizeof(output)), "join bounds and nulls");
}

int main(void)
{
    check(!mt_media_browser_visible_component("._Mine Storm.vec") &&
          !mt_media_browser_visible_component(".hidden") &&
          !mt_media_browser_visible_component("..") &&
          mt_media_browser_visible_component("game.v2.vec"), "hide dot entries without hiding ordinary dotted files");
    test_navigation();
    test_rejection();
    test_lengths();
    test_relative_join();
    puts("PASS: rooted media navigation, relative file paths, bounds and no escape");
    return 0;
}
