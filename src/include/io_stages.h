/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file io_stages.h
 * @brief Per-request NFS I/O path stage timings
 *
 * Absolute monotonic stamps on nfs_request_t; adjacent deltas aggregated
 * into global stats when Enable_IOStages is on.
 */

#ifndef IO_STAGES_H
#define IO_STAGES_H

#include <stdint.h>
#include <stdbool.h>
#include "gsh_config.h"
#include "common_utils.h"

/** Stage IDs — stamp order; unset = 0 ns */
enum io_stage_id {
	IO_STAGE_T00_EPOLL_READY = 0,
	IO_STAGE_T01_SUBMIT,
	IO_STAGE_T02_TASK_RECV,
	IO_STAGE_T03_SVC_REQUEST,
	IO_STAGE_T04_PROCESS,
	IO_STAGE_T04B_AUTH_DONE,
	IO_STAGE_T05_COMPOUND,
	IO_STAGE_T06_PRIMARY_OP,
	IO_STAGE_T07_FSAL_ENTER,
	IO_STAGE_T08_FSAL_DONE,
	IO_STAGE_T09_COMPLETE_COMPOUND,
	IO_STAGE_T10_COMPLETE_REQUEST,
	IO_STAGE_T11_REPLY_DONE,
	IO_STAGE_MAX
};

/** Rolled-up buckets for quick pre/post-FSAL analysis */
enum io_stage_bucket {
	IO_BUCKET_PRE_FSAL = 0, /* T07 - T04 */
	IO_BUCKET_FSAL, /* T08 - T07 */
	IO_BUCKET_POST_FSAL, /* T10 - T08 */
	IO_BUCKET_MAX
};

#define IO_STAGE_FLAG_ACTIVE 0x01

struct io_stage_timing {
	uint64_t ts_ns[IO_STAGE_MAX]; /* 0 = unset */
	uint16_t primary_op;
	uint8_t flags;
};

static inline bool io_stages_enabled(void)
{
	return unlikely(nfs_param.core_param.enable_IOSTAGES);
}

static inline uint64_t io_stage_now_ns(void)
{
	struct timespec ts;

	now_mono(&ts);
	return timespec_to_nsecs(&ts);
}

static inline void io_stage_activate(struct io_stage_timing *st)
{
	if (!st || !io_stages_enabled())
		return;
	st->flags |= IO_STAGE_FLAG_ACTIVE;
}

static inline void io_stage_stamp(struct io_stage_timing *st,
				  enum io_stage_id stage)
{
	if (!st || !io_stages_enabled() || stage >= IO_STAGE_MAX)
		return;
	if (!(st->flags & IO_STAGE_FLAG_ACTIVE))
		return;
	/* First stamp wins for FSAL_DONE (async resume / CLOSEFD) */
	if (stage == IO_STAGE_T08_FSAL_DONE && st->ts_ns[stage] != 0)
		return;
	st->ts_ns[stage] = io_stage_now_ns();
}

static inline void io_stage_stamp_ns(struct io_stage_timing *st,
				     enum io_stage_id stage, uint64_t ns)
{
	if (!st || !io_stages_enabled() || stage >= IO_STAGE_MAX || ns == 0)
		return;
	if (!(st->flags & IO_STAGE_FLAG_ACTIVE))
		return;
	if (st->ts_ns[stage] != 0)
		return;
	st->ts_ns[stage] = ns;
}

const char *io_stage_name(enum io_stage_id stage);
const char *io_stage_bucket_name(enum io_stage_bucket bucket);

#endif /* IO_STAGES_H */
