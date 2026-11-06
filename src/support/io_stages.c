/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file io_stages.c
 * @brief I/O path stage timing names and aggregation
 */

#include "config.h"
#include "fsal.h"
#include "nfs_core.h"
#include "io_stages.h"
#include "server_stats.h"
#include "gsh_dbus.h"
#include <dbus/dbus.h>
#include <string.h>

static const char *const stage_names[IO_STAGE_MAX] = {
	[IO_STAGE_T00_EPOLL_READY] = "T00_epoll_ready",
	[IO_STAGE_T01_SUBMIT] = "T01_submit",
	[IO_STAGE_T02_TASK_RECV] = "T02_task_recv",
	[IO_STAGE_T03_SVC_REQUEST] = "T03_svc_request",
	[IO_STAGE_T04_PROCESS] = "T04_process",
	[IO_STAGE_T04B_AUTH_DONE] = "T04b_auth_done",
	[IO_STAGE_T05_COMPOUND] = "T05_compound",
	[IO_STAGE_T06_PRIMARY_OP] = "T06_primary_op",
	[IO_STAGE_T07_FSAL_ENTER] = "T07_fsal_enter",
	[IO_STAGE_T08_FSAL_DONE] = "T08_fsal_done",
	[IO_STAGE_T09_COMPLETE_COMPOUND] = "T09_complete_compound",
	[IO_STAGE_T10_COMPLETE_REQUEST] = "T10_complete_request",
	[IO_STAGE_T11_REPLY_DONE] = "T11_reply_done",
};

static const char *const bucket_names[IO_BUCKET_MAX] = {
	[IO_BUCKET_PRE_FSAL] = "pre_fsal",
	[IO_BUCKET_FSAL] = "fsal",
	[IO_BUCKET_POST_FSAL] = "post_fsal",
};

struct io_stage_accum {
	uint64_t count;
	uint64_t sum_ns;
	uint64_t min_ns;
	uint64_t max_ns;
};

static struct io_stage_accum stage_delta[IO_STAGE_MAX];
static struct io_stage_accum stage_bucket[IO_BUCKET_MAX];
static uint64_t stage_samples;
struct timespec io_stages_stats_time;

const char *io_stage_name(enum io_stage_id stage)
{
	if (stage >= IO_STAGE_MAX)
		return "unknown";
	return stage_names[stage];
}

const char *io_stage_bucket_name(enum io_stage_bucket bucket)
{
	if (bucket >= IO_BUCKET_MAX)
		return "unknown";
	return bucket_names[bucket];
}

static void accum_add(struct io_stage_accum *a, uint64_t delta_ns)
{
	a->count++;
	a->sum_ns += delta_ns;
	if (a->min_ns == 0 || delta_ns < a->min_ns)
		a->min_ns = delta_ns;
	if (delta_ns > a->max_ns)
		a->max_ns = delta_ns;
}

static bool stage_delta_get(const struct io_stage_timing *st, int cur,
			    int prev, uint64_t *out)
{
	uint64_t c = st->ts_ns[cur];
	uint64_t p = st->ts_ns[prev];

	if (c == 0 || p == 0 || c < p)
		return false;
	*out = c - p;
	return true;
}

void server_stats_io_stages_done(struct nfs_request *req)
{
	struct io_stage_timing *st;
	int i;
	uint64_t d;

	if (!req || !io_stages_enabled())
		return;
	st = &req->io_stages;
	if (!(st->flags & IO_STAGE_FLAG_ACTIVE))
		return;

	/* Adjacent deltas: Ti - T(i-1) for set pairs in order.
	 * Include 0-ns gaps (inline epoll path sets T01==T00) so counts align.
	 */
	for (i = 1; i < IO_STAGE_T11_REPLY_DONE; i++) {
		int prev = i - 1;

		while (prev >= 0 && st->ts_ns[prev] == 0)
			prev--;
		if (prev < 0 || st->ts_ns[i] == 0)
			continue;
		if (stage_delta_get(st, i, prev, &d))
			accum_add(&stage_delta[i], d);
	}

	if (stage_delta_get(st, IO_STAGE_T07_FSAL_ENTER, IO_STAGE_T04_PROCESS,
			    &d))
		accum_add(&stage_bucket[IO_BUCKET_PRE_FSAL], d);

	if (stage_delta_get(st, IO_STAGE_T08_FSAL_DONE, IO_STAGE_T07_FSAL_ENTER,
			    &d))
		accum_add(&stage_bucket[IO_BUCKET_FSAL], d);

	if (stage_delta_get(st, IO_STAGE_T10_COMPLETE_REQUEST,
			    IO_STAGE_T08_FSAL_DONE, &d))
		accum_add(&stage_bucket[IO_BUCKET_POST_FSAL], d);

	stage_samples++;
}

void server_stats_io_stages_reply_done(uint64_t t10_ns)
{
	uint64_t now_ns;
	uint64_t d;

	if (!io_stages_enabled() || t10_ns == 0)
		return;
	now_ns = io_stage_now_ns();
	if (now_ns < t10_ns)
		return;
	d = now_ns - t10_ns;
	accum_add(&stage_delta[IO_STAGE_T11_REPLY_DONE], d);
}

void server_stats_io_stages_reset(void)
{
	memset(stage_delta, 0, sizeof(stage_delta));
	memset(stage_bucket, 0, sizeof(stage_bucket));
	stage_samples = 0;
	now(&io_stages_stats_time);
}

static void dbus_append_accum(DBusMessageIter *array_iter, const char *name,
			      const struct io_stage_accum *a)
{
	DBusMessageIter op_iter;
	double avg_us = 0.0, min_us = 0.0, max_us = 0.0;

	if (a->count > 0) {
		avg_us = (double)a->sum_ns * 1e-3 / (double)a->count;
		min_us = (double)a->min_ns * 1e-3;
		max_us = (double)a->max_ns * 1e-3;
	}

	dbus_message_iter_open_container(array_iter, DBUS_TYPE_STRUCT, NULL,
					 &op_iter);
	dbus_message_iter_append_basic(&op_iter, DBUS_TYPE_STRING, &name);
	dbus_message_iter_append_basic(&op_iter, DBUS_TYPE_UINT64, &a->count);
	dbus_message_iter_append_basic(&op_iter, DBUS_TYPE_DOUBLE, &avg_us);
	dbus_message_iter_append_basic(&op_iter, DBUS_TYPE_DOUBLE, &min_us);
	dbus_message_iter_append_basic(&op_iter, DBUS_TYPE_DOUBLE, &max_us);
	dbus_message_iter_close_container(array_iter, &op_iter);
}

void server_dbus_io_stages_stats(DBusMessageIter *iter)
{
	DBusMessageIter array_iter;
	int i;
	char *message;

	gsh_dbus_append_timestamp(iter, &io_stages_stats_time);
	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "(stddd)",
					 &array_iter);

	for (i = 0; i < IO_BUCKET_MAX; i++)
		dbus_append_accum(&array_iter, bucket_names[i],
				  &stage_bucket[i]);

	for (i = 1; i < IO_STAGE_MAX; i++)
		dbus_append_accum(&array_iter, stage_names[i], &stage_delta[i]);

	dbus_message_iter_close_container(iter, &array_iter);

	if (stage_samples == 0)
		message = "None";
	else
		message = "OK";
	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &message);
}
