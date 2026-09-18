/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_TEXT_DOCUMENT_H
#define MULTITINI_TEXT_DOCUMENT_H

#include "multitini_text_reader.h"

typedef enum {
    MT_TEXT_DOCUMENT_OK = 0,
    MT_TEXT_DOCUMENT_INVALID_PATH,
    MT_TEXT_DOCUMENT_NO_MEMORY,
    MT_TEXT_DOCUMENT_OPEN_FAILED,
    MT_TEXT_DOCUMENT_READ_FAILED,
    MT_TEXT_DOCUMENT_CLOSE_FAILED,
    MT_TEXT_DOCUMENT_DECODE_FAILED
} mt_text_document_result_t;

/* The caller resolves/validates the path before loading. reader must already
 * be initialized. Every failure leaves it closed. Reads use at most 4 KiB
 * per call and stop after INPUT_LIMIT bytes, using file size for truncation.
 * The temporary file is closed before opening the text reader.
 * progress is optional and runs after each nonempty successful read. It must
 * not reenter storage or core/menu actions while the file is open. */
mt_text_document_result_t mt_text_document_load(mt_text_reader_t *reader,
                                                const char *path,
                                                size_t columns,
                                                size_t page_rows,
                                                void (*progress)(void *),
                                                void *context);

#endif
