/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VIDEO_FRAME_SERVICE_H
#define MULTITINI_VIDEO_FRAME_SERVICE_H
#include "video_effect_service.h"
#include "multitini_video_frame.h"
void mt_video_frame_service_init(const mt_video_effect_service_ops_t *ops);
void mt_video_frame_service_stop(void);
bool mt_video_frame_service_quiescent(void);
/* Start ordered hardware processing, or update options without stopping it. */
void mt_video_frame_service_start(uint32_t epoch, const mt_video_frame_options_t *options);
/* Monitoring only; hardware frame progress never depends on this poll. */
void mt_video_frame_service_poll(void);
/* Cached failure state: querying it never accesses PL or changes ownership. */
bool mt_video_frame_service_failed(void);
const char *mt_video_frame_service_status(void);
#endif
