/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright (C) 2026
 *
 * VFS/XFS FSAL statistics: prepare, record, extract (DBus), and reset.
 * Patterned after FSAL_CEPH/fsal_stats_ceph.c and FSAL_GPFS/fsal_stats_gpfs.c.
 *
 * Linked into both fsalvfs and fsalxfs so each module has its own counters.
 */

#include "config.h"
#include "gsh_list.h"
#ifdef USE_DBUS
#include "gsh_dbus.h"
#endif
#include <abstract_atomic.h>
#include "fsal.h"
#include "vfs_stats.h"

struct fsal_op_stats vfs_op_stats[VFS_STAT_MAX];
struct fsal_stats vfs_stats;

#ifdef USE_DBUS
/**
 * @brief Return display name for a VFS FSAL stat op
 */
static char *vfs_opcode_to_name(int opcode)
{
	switch (opcode) {
	case VFS_STAT_LOOKUP:
		return "LOOKUP";
	case VFS_STAT_GETATTR:
		return "GETATTR";
	case VFS_STAT_SETATTR:
		return "SETATTR";
	case VFS_STAT_CREATE:
		return "CREATE";
	case VFS_STAT_OPEN:
		return "OPEN";
	case VFS_STAT_CLOSE:
		return "CLOSE";
	case VFS_STAT_READ:
		return "READ";
	case VFS_STAT_WRITE:
		return "WRITE";
	case VFS_STAT_MKDIR:
		return "MKDIR";
	case VFS_STAT_MKNOD:
		return "MKNOD";
	case VFS_STAT_SYMLINK:
		return "SYMLINK";
	case VFS_STAT_READLINK:
		return "READLINK";
	case VFS_STAT_LINK:
		return "LINK";
	case VFS_STAT_UNLINK:
		return "UNLINK";
	case VFS_STAT_RMDIR:
		return "RMDIR";
	case VFS_STAT_RENAME:
		return "RENAME";
	case VFS_STAT_FSYNC:
		return "FSYNC";
	case VFS_STAT_GETLK:
		return "GETLK";
	case VFS_STAT_SETLK:
		return "SETLK";
	case VFS_STAT_GETXATTR:
		return "GETXATTR";
	case VFS_STAT_SETXATTR:
		return "SETXATTR";
	case VFS_STAT_REMOVEXATTR:
		return "REMOVEXATTR";
	case VFS_STAT_LISTXATTR:
		return "LISTXATTR";
	case VFS_STAT_FALLOCATE:
		return "FALLOCATE";
	default:
		return "UNMONITORED";
	}
}
#endif /* USE_DBUS */

void vfs_fsal_record_stats(enum vfs_stat_op op, uint64_t resp_time)
{
	int idx = (int)op;

	if (idx < 0 || idx >= VFS_STAT_MAX)
		return;

	(void)atomic_inc_uint64_t(&vfs_stats.op_stats[idx].num_ops);
	(void)atomic_add_uint64_t(&vfs_stats.op_stats[idx].resp_time,
				  resp_time);
	if (vfs_stats.op_stats[idx].resp_time_max < resp_time)
		vfs_stats.op_stats[idx].resp_time_max = resp_time;
	if (vfs_stats.op_stats[idx].resp_time_min == 0 ||
	    vfs_stats.op_stats[idx].resp_time_min > resp_time)
		vfs_stats.op_stats[idx].resp_time_min = resp_time;
}

/** @brief Attach VFS stats storage to the FSAL module handle */
void vfs_prepare_for_stats(struct fsal_module *fsal_hdl)
{
	int op;

	vfs_stats.total_ops = VFS_STAT_MAX;
	vfs_stats.op_stats = vfs_op_stats;
	fsal_hdl->stats = &vfs_stats;

	for (op = 0; op < VFS_STAT_MAX; op++)
		fsal_hdl->stats->op_stats[op].op_code = op;
}

#ifdef USE_DBUS
/** @brief Pack VFS/XFS FSAL performance counters into a DBus reply */
void vfs_fsal_extract_stats(struct fsal_module *fsal_hdl, void *iter)
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
	/* Use the registered module name (VFS, XFS, LUSTRE, ...). */
	message = (char *)fsal_hdl->name;
	dbus_message_iter_append_basic(iter1, DBUS_TYPE_STRING, &message);

	dbus_message_iter_open_container(iter1, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	for (i = 0; i < VFS_STAT_MAX; i++) {
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

		message = vfs_opcode_to_name(stats->op_stats[i].op_code);
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING,
					       &message);
		dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
					       &total_ops);
		/* Convert nsecs to milliseconds (same as GPFS/Ceph) */
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

void vfs_fsal_reset_stats(struct fsal_module *fsal_hdl)
{
	int i;
	struct fsal_stats *stats = fsal_hdl->stats;

	if (stats == NULL)
		return;

	for (i = 0; i < VFS_STAT_MAX; i++) {
		atomic_store_uint64_t(&stats->op_stats[i].num_ops, 0);
		atomic_store_uint64_t(&stats->op_stats[i].resp_time, 0);
		atomic_store_uint64_t(&stats->op_stats[i].resp_time_min, 0);
		atomic_store_uint64_t(&stats->op_stats[i].resp_time_max, 0);
	}
}
