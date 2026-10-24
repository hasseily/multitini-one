# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual board document filtering with the real core/path policies."""
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
WRAPPER = ROOT / "scripts/run_native_tool.py"


def production_function(source: str, signature: str) -> str:
    start = source.index(signature)
    end = source.index("\n}", start) + len("\n}")
    return source[start:end]


PREAMBLE = r'''
#include "multitini_core_frontend.h"
#include "multitini_media_browser.h"
#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#include "multitini_vectrex_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(v) do { if (!(v)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #v); exit(1); \
} } while (0)
static mt_supervisor_t supervisor;
static struct {
    uint64_t before;
    mt_menu_file_t files[MT_MENU_MAX_FILES];
    uint64_t after;
} guarded;
#define supervisor_menu_files guarded.files
static size_t supervisor_menu_file_count;
static unsigned iigs_browser_drive;
'''


SCENARIOS = r'''
static void reset(uint32_t core_id)
{
    memset(&supervisor, 0, sizeof(supervisor));
    memset(&guarded, 0, sizeof(guarded));
    supervisor.has_active = core_id != 0U;
    supervisor.active.package.core_id = core_id;
    supervisor_menu_file_count = 0U;
    guarded.before = UINT64_C(0x123456789ABCDEF0);
    guarded.after = UINT64_C(0x0FEDCBA987654321);
}

static void collect(mt_menu_browser_t browser, const char *name, bool directory)
{
    CHECK(collect_menu_file(&browser, name, 123U, directory) == 0);
    CHECK(supervisor_menu_file_count <= MT_MENU_MAX_FILES);
    CHECK(guarded.before == UINT64_C(0x123456789ABCDEF0));
    CHECK(guarded.after == UINT64_C(0x0FEDCBA987654321));
}

static size_t find(const char *name)
{
    for (size_t i = 0U; i < supervisor_menu_file_count; ++i)
        if (strcmp(supervisor_menu_files[i].name, name) == 0) return i;
    return SIZE_MAX;
}

static void test_names(void)
{
    static const char *const valid[] = {"notes.txt", "notes.TXT", "notes.TxT", "notes.tXt"};
    static const char *const invalid[] = {
        NULL, "", ".", "..", "../notes.txt", "folder/notes.txt", "folder\\notes.txt",
        "0:notes.txt", "a:notes.txt", "bad?.txt", "bad*.txt", "bad|.txt",
        "bad<.txt", "bad>.txt", "bad\".txt", "bad\t.txt", "bad\n.txt",
        "notes.txt ", "notes.txt.", "notes.txt.bak", "notes.md"
    };
    char long_name[MT_MENU_FILE_NAME_MAX + 1U];
    reset(0U);
    for (size_t i = 0U; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        CHECK(is_text_document(valid[i]));
        collect(MT_MENU_BROWSER_DOCUMENTS, valid[i], false);
        CHECK(supervisor_menu_file_count == i + 1U);
        CHECK(supervisor_menu_files[i].cookie == MT_MENU_FILE_TEXT);
        CHECK(supervisor_menu_files[i].size == 123U);
    }
    CHECK(!is_text_document(NULL) && !is_text_document("notes.txt.bak"));
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        collect(MT_MENU_BROWSER_DOCUMENTS, invalid[i], false);
    CHECK(supervisor_menu_file_count == sizeof(valid) / sizeof(valid[0]));
    memset(long_name, 'a', sizeof(long_name));
    memcpy(long_name + MT_MENU_FILE_NAME_MAX - 5U, ".txt", 5U);
    collect(MT_MENU_BROWSER_DOCUMENTS, long_name, false);
    CHECK(find(long_name) != SIZE_MAX); /* Longest terminated menu filename. */
    memset(long_name, 'a', sizeof(long_name));
    memcpy(long_name + MT_MENU_FILE_NAME_MAX - 4U, ".txt", 5U);
    collect(MT_MENU_BROWSER_DOCUMENTS, long_name, false);
    CHECK(supervisor_menu_file_count == sizeof(valid) / sizeof(valid[0]) + 1U);
}

static void test_standby_documents(void)
{
    static const char *const media[] = {"game.prg", "game.d64", "game.crt", "game.gbc", "game.vec", "game.bas"};
    reset(0U);
    collect(MT_MENU_BROWSER_DOCUMENTS, "Guide.TXT", false);
    collect(MT_MENU_BROWSER_DOCUMENTS, "Manuals", true);
    collect(MT_MENU_BROWSER_DOCUMENTS, "folder.txt", true);
    for (size_t i = 0U; i < sizeof(media) / sizeof(media[0]); ++i)
        collect(MT_MENU_BROWSER_DOCUMENTS, media[i], false);
    CHECK(supervisor_menu_file_count == 3U);
    CHECK(supervisor_menu_files[0].cookie == MT_MENU_FILE_TEXT);
    CHECK(supervisor_menu_files[1].cookie == MT_MENU_FILE_DIRECTORY);
    CHECK(supervisor_menu_files[2].cookie == MT_MENU_FILE_DIRECTORY);
    reset(MT_C64_CORE_ID);
    for (size_t i = 0U; i < sizeof(media) / sizeof(media[0]); ++i)
        collect(MT_MENU_BROWSER_DOCUMENTS, media[i], false);
    CHECK(supervisor_menu_file_count == 0U); /* Active core cannot widen documents. */
}

static void test_media_and_directories(void)
{
    reset(MT_C64_CORE_ID);
    collect(MT_MENU_BROWSER_PROGRAM, "game.prg", false);
    collect(MT_MENU_BROWSER_PROGRAM, "readme.txt", false);
    collect(MT_MENU_BROWSER_PROGRAM, "game.gbc", false);
    collect(MT_MENU_BROWSER_PROGRAM, "subfolder", true);
    CHECK(supervisor_menu_file_count == 3U);
    CHECK(supervisor_menu_files[0].cookie == 0U && supervisor_menu_files[1].cookie == MT_MENU_FILE_TEXT);
    CHECK(supervisor_menu_files[2].cookie == MT_MENU_FILE_DIRECTORY);
    reset(MT_C64_CORE_ID);
    collect(MT_MENU_BROWSER_DRIVE8, "disk.d64", false);
    collect(MT_MENU_BROWSER_DRIVE8, "disk.g64", false);
    collect(MT_MENU_BROWSER_DRIVE8, "guide.txt", false);
    collect(MT_MENU_BROWSER_DRIVE8, "game.prg", false);
    collect(MT_MENU_BROWSER_DRIVE8, "disks", true);
    CHECK(supervisor_menu_file_count == 4U);
    CHECK(supervisor_menu_files[3].cookie == MT_MENU_FILE_DIRECTORY);
    reset(MT_GBC_CORE_ID);
    collect(MT_MENU_BROWSER_CARTRIDGE, "game.gb", false);
    collect(MT_MENU_BROWSER_CARTRIDGE, "game.gbc", false);
    collect(MT_MENU_BROWSER_CARTRIDGE, "guide.txt", false);
    collect(MT_MENU_BROWSER_CARTRIDGE, "folder", true);
    CHECK(supervisor_menu_file_count == 3U);
    reset(MT_VECTREX_CORE_ID);
    collect(MT_MENU_BROWSER_CARTRIDGE, "game.bin", false);
    collect(MT_MENU_BROWSER_CARTRIDGE, "game.vec", false);
    collect(MT_MENU_BROWSER_CARTRIDGE, "guide.txt", false);
    collect(MT_MENU_BROWSER_CARTRIDGE, "folder", true);
    CHECK(supervisor_menu_file_count == 4U);
    CHECK(supervisor_menu_files[3].cookie == MT_MENU_FILE_DIRECTORY);
    reset(0U);
    supervisor.active.package.core_id = MT_VECTREX_CORE_ID; /* Stale identity must not enable folders. */
    collect(MT_MENU_BROWSER_PROGRAM, "folder", true);
    collect(MT_MENU_BROWSER_PROGRAM, "game.prg", false);
    CHECK(supervisor_menu_file_count == 0U);
}

static void fill_text(mt_menu_browser_t browser)
{
    char name[32];
    while (supervisor_menu_file_count < MT_MENU_MAX_FILES) {
        (void)snprintf(name, sizeof(name), "notes%03u.txt", (unsigned)supervisor_menu_file_count);
        collect(browser, name, false);
    }
}

static void test_full_list_priority(void)
{
    char name[32];
    mt_menu_file_t saved[MT_MENU_MAX_FILES];
    reset(MT_C64_CORE_ID);
    fill_text(MT_MENU_BROWSER_PROGRAM);
    memcpy(saved, supervisor_menu_files, sizeof(saved));
    collect(MT_MENU_BROWSER_PROGRAM, "extra.txt", false);
    CHECK(memcmp(saved, supervisor_menu_files, sizeof(saved)) == 0);
    for (size_t i = 0U; i < MT_MENU_MAX_FILES; ++i) {
        (void)snprintf(name, sizeof(name), "game%03u.prg", (unsigned)i);
        collect(MT_MENU_BROWSER_PROGRAM, name, false);
        CHECK(find(name) != SIZE_MAX);
    }
    CHECK(supervisor_menu_file_count == MT_MENU_MAX_FILES);
    for (size_t i = 0U; i < MT_MENU_MAX_FILES; ++i)
        CHECK(supervisor_menu_files[i].cookie == 0U);
    memcpy(saved, supervisor_menu_files, sizeof(saved));
    collect(MT_MENU_BROWSER_PROGRAM, "extra.prg", false);
    collect(MT_MENU_BROWSER_PROGRAM, "extra.txt", false);
    CHECK(memcmp(saved, supervisor_menu_files, sizeof(saved)) == 0);

    reset(MT_C64_CORE_ID);
    collect(MT_MENU_BROWSER_PROGRAM, "first.txt", false);
    for (size_t i = 1U; i < MT_MENU_MAX_FILES; ++i) {
        (void)snprintf(name, sizeof(name), "game%03u.prg", (unsigned)i);
        collect(MT_MENU_BROWSER_PROGRAM, name, false);
    }
    collect(MT_MENU_BROWSER_PROGRAM, "new.prg", false);
    CHECK(find("first.txt") == SIZE_MAX && find("new.prg") != SIZE_MAX);
    for (size_t i = 1U; i < MT_MENU_MAX_FILES; ++i) {
        (void)snprintf(name, sizeof(name), "game%03u.prg", (unsigned)i);
        CHECK(find(name) != SIZE_MAX); /* Moving the tail does not drop any prior media. */
    }

    reset(0U);
    strcpy(supervisor_menu_files[0].name, "..");
    supervisor_menu_files[0].cookie = MT_MENU_FILE_DIRECTORY | MT_MENU_FILE_PARENT;
    supervisor_menu_file_count = 1U;
    fill_text(MT_MENU_BROWSER_DOCUMENTS);
    collect(MT_MENU_BROWSER_DOCUMENTS, "Manuals", true);
    CHECK(find("Manuals") != SIZE_MAX && supervisor_menu_file_count == MT_MENU_MAX_FILES);
    CHECK(strcmp(supervisor_menu_files[0].name, "..") == 0);
    CHECK(supervisor_menu_files[0].cookie == (MT_MENU_FILE_DIRECTORY | MT_MENU_FILE_PARENT));
    CHECK(supervisor_menu_files[find("Manuals")].cookie == MT_MENU_FILE_DIRECTORY);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    if (strcmp(argv[1], "names") == 0) test_names();
    else if (strcmp(argv[1], "standby") == 0) test_standby_documents();
    else if (strcmp(argv[1], "media") == 0) test_media_and_directories();
    else if (strcmp(argv[1], "priority") == 0) test_full_list_priority();
    else CHECK(false);
    printf("PASS: document browser runtime %s\n", argv[1]);
    return 0;
}
'''


class DocumentBrowserRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
        if compiler is None and os.name == "nt":
            candidate = Path("C:/msys64/ucrt64/bin/gcc.exe")
            if candidate.is_file():
                compiler = str(candidate)
        if compiler is None:
            raise unittest.SkipTest("A host C compiler is required for document browser runtime checks")
        cls.temporary = tempfile.TemporaryDirectory(prefix="multitini-document-browser-")
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        capacity = re.search(r"^#define MT_MENU_MAX_FILES .+$", source, re.MULTILINE)
        if capacity is None:
            raise AssertionError("Production browser capacity definition was not found")
        harness = directory / "document_browser_runtime.c"
        harness.write_text(capacity.group(0) + "\n" + PREAMBLE + "\n" + "\n\n".join(
            production_function(source, signature) for signature in (
                "static bool is_text_document(", "static int collect_menu_file(",
            )
        ) + "\n" + SCENARIOS)
        cls.executable = directory / ("document_browser_runtime.exe" if os.name == "nt" else "document_browser_runtime")
        result = subprocess.run([
            sys.executable, str(WRAPPER), "--", compiler, "-std=c11",
            "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-Wconversion",
            "-Wshadow", "-Wstrict-prototypes", "-I", str(ROOT / "firmware/include"),
            str(harness), str(ROOT / "firmware/common/core_frontend.c"),
            str(ROOT / "firmware/common/c64_features.c"),
            str(ROOT / "firmware/common/content_type.c"),
            str(ROOT / "firmware/common/genesis_content.c"),
            str(ROOT / "firmware/common/nes_content.c"),
            str(ROOT / "firmware/common/media_browser.c"),
            str(ROOT / "firmware/common/core_link.c"),
            str(ROOT / "firmware/common/vectrex_overlay.c"), "-o", str(cls.executable),
        ], capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_scenario(self, scenario):
        result = subprocess.run([
            sys.executable, str(WRAPPER), "--", str(self.executable), scenario,
        ], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"PASS: document browser runtime {scenario}", result.stdout)

    def test_mixed_case_extensions_invalid_components_and_name_bounds(self):
        self.run_scenario("names")

    def test_documents_browser_is_core_independent_and_excludes_media(self):
        self.run_scenario("standby")

    def test_txt_coexists_with_core_media_and_preserves_directory_policy(self):
        self.run_scenario("media")

    def test_full_lists_prioritize_media_and_folders_without_dropping_other_entries(self):
        self.run_scenario("priority")


if __name__ == "__main__":
    unittest.main()
