/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright (C) 2026
 *
 * Ceph FSAL statistics: prepare, record, extract (DBus), and reset.
 * Patterned after FSAL_GPFS/fsal_stats_gpfs.c.
 */

#include "config.h"
#include "gsh_list.h"
#ifdef USE_DBUS
#include "gsh_dbus.h"
#endif
#include <abstract_atomic.h>
#include "fsal.h"
#include "ceph_stats.h"

struct fsal_op_stats ceph_op_stats[CEPH_STAT_MAX];
struct fsal_stats ceph_stats;

#ifdef USE_DBUS
/**
 * @brief Return display name for a Ceph FSAL stat op
 */
static char *ceph_opcode_to_name(int opcode)
{
	switch (opcode) {
	case CEPH_STAT_LOOKUP:
		return "LOOKUP";
	case CEPH_STAT_GETATTR:
		return "GETATTR";
	case CEPH_STAT_SETATTR:
		return "SETATTR";
	case CEPH_STAT_CREATE:
		return "CREATE";
	case CEPH_STAT_OPEN:
		return "OPEN";
	case CEPH_STAT_CLOSE:
		return "CLOSE";
	case CEPH_STAT_READ:
		return "READ";
	case CEPH_STAT_WRITE:
		return "WRITE";
	case CEPH_STAT_MKDIR:
		return "MKDIR";
	case CEPH_STAT_MKNOD:
		return "MKNOD";
	case CEPH_STAT_SYMLINK:
		return "SYMLINK";
	case CEPH_STAT_READLINK:
		return "READLINK";
	case CEPH_STAT_LINK:
		return "LINK";
	case CEPH_STAT_UNLINK:
		return "UNLINK";
	case CEPH_STAT_RMDIR:
		return "RMDIR";
	case CEPH_STAT_RENAME:
		return "RENAME";
	case CEPH_STAT_OPENDIR:
		return "OPENDIR";
	case CEPH_STAT_RELEASEDIR:
		return "RELEASEDIR";
	case CEPH_STAT_FSYNC:
		return "FSYNC";
	case CEPH_STAT_GETLK:
		return "GETLK";
	case CEPH_STAT_SETLK:
		return "SETLK";
	case CEPH_STAT_GETXATTR:
		return "GETXATTR";
	case CEPH_STAT_SETXATTR:
		return "SETXATTR";
	case CEPH_STAT_REMOVEXATTR:
		return "REMOVEXATTR";
	case CEPH_STAT_FALLOCATE:
		return "FALLOCATE";
	case CEPH_STAT_WALK:
		return "WALK";
	case CEPH_STAT_DS_READ:
		return "DS_READ";
	case CEPH_STAT_DS_WRITE:
		return "DS_WRITE";
	default:
		return "UNMONITORED";
	}
}
#endif /* USE_DBUS */

void ceph_fsal_record_stats(enum ceph_stat_op op, uint64_t resp_time)
{
	int idx = (int)op;

	if (idx < 0 || idx >= CEPH_STAT_MAX)
		return;

	(void)atomic_inc_uint64_t(&ceph_stats.op_stats[idx].num_ops);
	(void)atomic_add_uint64_t(&ceph_stats.op_stats[idx].resp_time,
				  resp_time);
	if (ceph_stats.op_stats[idx].resp_time_max < resp_time)
		ceph_stats.op_stats[idx].resp_time_max = resp_time;
	if (ceph_stats.op_stats[idx].resp_time_min == 0 ||
	    ceph_stats.op_stats[idx].resp_time_min > resp_time)
		ceph_stats.op_stats[idx].resp_time_min = resp_time;
}

/** @brief Attach Ceph stats storage to the FSAL module handle */
void ceph_prepare_for_stats(struct fsal_module *fsal_hdl)
{
	int op;

	ceph_stats.total_ops = CEPH_STAT_MAX;
	ceph_stats.op_stats = ceph_op_stats;
	fsal_hdl->stats = &ceph_stats;

	for (op = 0; op < CEPH_STAT_MAX; op++)
		fsal_hdl->stats->op_stats[op].op_code = op;
}

#ifdef USE_DBUS
/** @brief Pack Ceph FSAL performance counters into a DBus reply */
void ceph_fsal_extract_stats(struct fsal_module *fsal_hdl, void *iter)
{
	DBusMessageIter struct_iter;
	DBusMessageIter *iter1 = (DBusMessageIter *)iter;
	char *message;
	uint64_t total_ops = 0, total_resp, min_resp, max_resp;
	uint64_t op_counter = 0;
	double res = 0.0;
	int i;
	struct fsal_stats *stats;

	stats = fsal_hdl->stats;
	message = "Ceph";
	dbus_message_iter_append_basic(iter1, DBUS_TYPE_STRING, &message);

	dbus_message_iter_open_container(iter1, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	for (i = 0; i < CEPH_STAT_MAX; i++) {
		total_ops =
			atomic_fetch_uint64_t(&stats->op_stats[i].num_ops);
		if (total_ops == 0)
			continue;

		total_resp = atomic_fetch_uint64_t(
			&stats->op_stats[i].resp_time);
		min_resp = atomic_fetch_uint64_t(
			&stats->op_stats[i].resp_time_min);
		max_resp = atomic_fetch_uint64_t(
			&stats->op_stats[i].resp_time_max);

		message = ceph_opcode_to_name(stats->op_stats[i].op_code);
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
					       &message);
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					       &total_ops);
		/* Convert nsecs to milliseconds (same as GPFS) */
		res = (double)total_resp * 0.000001 / total_ops;
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_DOUBLE,
					       &res);
		res = (double)min_resp * 0.000001;
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_DOUBLE,
					       &res);
		res = (double)max_resp * 0.000001;
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_DOUBLE,
					       &res);
		op_counter += total_ops;
	}
	if (op_counter == 0) {
		message = "None";
		/* insert dummy stats to avoid dbus crash */
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
					       &message);
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					       &total_ops);
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_DOUBLE,
					       &res);
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_DOUBLE,
					       &res);
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_DOUBLE,
					       &res);
	} else {
		message = "OK";
	}
	dbus_message_iter_close_container(iter1, &struct_iter);
	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &message);
}
#endif /* USE_DBUS */

void ceph_fsal_reset_stats(struct fsal_module *fsal_hdl)
{
	int i;
	struct fsal_stats *stats = fsal_hdl->stats;

	if (stats == NULL)
		return;

	for (i = 0; i < CEPH_STAT_MAX; i++) {
		atomic_store_uint64_t(&stats->op_stats[i].num_ops, 0);
		atomic_store_uint64_t(&stats->op_stats[i].resp_time, 0);
		atomic_store_uint64_t(&stats->op_stats[i].resp_time_min, 0);
		atomic_store_uint64_t(&stats->op_stats[i].resp_time_max, 0);
	}
}
