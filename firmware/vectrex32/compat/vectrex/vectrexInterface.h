/* SPDX-License-Identifier: GPL-3.0-only
 * Minimal embedding boundary for the original PiTrex GS BASIC language files.
 * No PiTrex hardware, USB or rendering implementation is included. */
#ifndef MT_V32_COMPAT_VECTREX_H
#define MT_V32_COMPAT_VECTREX_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_MSC_VER)
static inline int strcasecmp(const char *left, const char *right)
{ return _stricmp(left, right); }
static inline int strncasecmp(const char *left, const char *right, size_t count)
{ return _strnicmp(left, right, count); }
#else
#include <strings.h>
#endif
#include <limits.h>
#include <math.h>
extern int usbKeyboardAvailable;
void v_WaitRecal(void);
void v_message(const char *message);
void v_noSound(void);
void v_writePSG_double_buffered(unsigned reg, unsigned value);
unsigned v_readPSG_double_buffered(unsigned reg);
int mt_v32_poll_statement(unsigned line);
#endif
