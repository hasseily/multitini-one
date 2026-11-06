/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_ABOUT_CREDITS_H
#define MULTITINI_ABOUT_CREDITS_H

/* Contributor and signature wording follows Appletini ONE's About screen.
 * The source pins and per-file notices cover the full set of dependencies. */
static const char *const about_contributors[] = {
    "Hardware - Karl \"KKR75\" Asseily",
    "Firmware - Henri \"Rikkles\" Asseily"
};

static const char *const about_intro[] = {
    "The firmware is open-source and available on GitHub. We only stand on the shoulders of giants.",
    "github.com/hasseily/multitini-one"
};

static const char about_signature[] = "Rikkles && KKR75 - Yarze, Lebanon";

static const char *const about_credits[] = {
    "This build uses and adapts Atari-XT by ThrudTheBarbarian.",
    "https://github.com/ThrudTheBarbarian/fpga-xt",
    "",
    "Appletini project origin - John \"Elltwo\" Flanagan",
    "Appletini ONE - board design and shared platform code",
    "MiSTer FPGA - Sorgelig and the MiSTer community",
    "",
    "C64 - MiSTer C64, based on Peter Wendrich's FPGA64",
    "Game Boy Color - MiSTer Gameboy, Till Harbaum and SameBoy",
    "Vectrex - Greg Miller's 6809 via JTFRAME, Thomas Skibo's VIA, and AY audio by MikeJ and Sorgelig",
    "Vectrex32 support - Robert E. Alexander's GS BASIC via PiTrex and Valavan Manohararajah's 6809 via VecX",
    "Genesis / Mega Drive - Nuked-MD by nukeykt, ogamespec, and andkorzh via MiSTer",
    "NES - MiSTer NES, Ludvig Strigeus and the T65 contributors",
    "Apple IIgs - MiSTer Apple IIgs by Pierre Cornier, Steven A. Wilson, Alan Steremberg, Frank Bruno and Jim Gregory",
    "Apple IIgs floppy codec - CiderPress II by Andy McFadden",
    "Apple IIgs UART - Gisselquist Technology",
    "Amiga - MiSTer Minimig, originally by Dennis van Weeren, with contributions from Rok Krajnc and others",
    "Amiga CPUs - FX68K by Jorge Cwik, TG68K by Tobias Gubener and AP68040 by Adam Polkosnik",
    "ZX Spectrum - MiSTer ZX Spectrum by Sorgelig, T80 by Daniel Wallner, and AY audio by MikeJ and Sorgelig",
    "",
    "CherryUSB - embedded USB stack",
    "FatFs - FAT filesystem module by ChaN",
    "AMD Xilinx - Vitis standalone BSP and drivers",
    "C64 reference projects - VICE, MAME and 1541 Ultimate",
    "",
    "Source pins and licenses are in provenance/sources.lock.json and licenses/files.csv."
};

#endif
