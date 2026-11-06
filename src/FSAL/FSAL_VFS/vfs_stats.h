/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright (C) 2026
 *
 * VFS/XFS FSAL operation statistics (patterned after FSAL_CEPH / FSAL_GPFS).
 */

#ifndef FSAL_VFS_STATS_H
#define FSAL_VFS_STATS_H

#include "fsal.h"
#include "common_utils.h"
#include "nfs_core.h"

/**
 * Indices for VFS FSAL stats. Contiguous so they map 1:1 into
 * fsal_op_stats[]. Keep in sync with vfs_opcode_to_name().
 */
enum vfs_stat_op {
	VFS_STAT_LOOKUP = 0,
	VFS_STAT_GETATTR,
	VFS_STAT_SETATTR,
	VFS_STAT_CREATE,
	VFS_STAT_OPEN,
	VFS_STAT_CLOSE,
	VFS_STAT_READ,
	VFS_STAT_WRITE,
	VFS_STAT_MKDIR,
	VFS_STAT_MKNOD,
	VFS_STAT_SYMLINK,
	VFS_STAT_READLINK,
	VFS_STAT_LINK,
	VFS_STAT_UNLINK,
	VFS_STAT_RMDIR,
	VFS_STAT_RENAME,
	VFS_STAT_FSYNC,
	VFS_STAT_GETLK,
	VFS_STAT_SETLK,
	VFS_STAT_GETXATTR,
	VFS_STAT_SETXATTR,
	VFS_STAT_REMOVEXATTR,
	VFS_STAT_LISTXATTR,
	VFS_STAT_FALLOCATE,
	VFS_STAT_MAX
};

extern struct fsal_stats vfs_stats;

void vfs_prepare_for_stats(struct fsal_module *fsal_hdl);
#ifdef USE_DBUS
void vfs_fsal_extract_stats(struct fsal_module *fsal_hdl, void *iter);
#endif
void vfs_fsal_reset_stats(struct fsal_module *fsal_hdl);
void vfs_fsal_record_stats(enum vfs_stat_op op, uint64_t resp_time);

/**
 * Time a VFS/XFS backend call and record FSAL stats when enabled.
 *
 * Usage:
 *   VFS_FSAL_TIME_CALL(VFS_STAT_READ, nb_read = preadv(...));
 */
#define VFS_FSAL_TIME_CALL(op, call)                                           \
	do {                                                                   \
		if (nfs_param.core_param.enable_FSALSTATS) {                   \
			struct timespec _vfs_stat_start, _vfs_stat_stop;       \
			now(&_vfs_stat_start);                                 \
			call;                                                  \
			now(&_vfs_stat_stop);                                  \
			vfs_fsal_record_stats(                                 \
				(op),                                          \
				timespec_diff(&_vfs_stat_start,                \
					      &_vfs_stat_stop));               \
		} else {                                                       \
			call;                                                  \
		}                                                              \
	} while (0)

/**
 * Like VFS_FSAL_TIME_CALL, but includes a post-I/O busy-wait in the
 * measured interval (see vfs_io_busy_delay).
 *
 * Usage:
 *   VFS_FSAL_TIME_CALL_BUSY(VFS_STAT_READ,
 *                           vfs_io_busy_delay(obj_hdl->fsal),
 *                           nb_read = preadv(...));
 */
#define VFS_FSAL_TIME_CALL_BUSY(op, busy, call)                                \
	do {                                                                   \
		if (nfs_param.core_param.enable_FSALSTATS) {                   \
			struct timespec _vfs_stat_start, _vfs_stat_stop;       \
			now(&_vfs_stat_start);                                 \
			call;                                                  \
			busy;                                                  \
			now(&_vfs_stat_stop);                                  \
			vfs_fsal_record_stats(                                 \
				(op),                                          \
				timespec_diff(&_vfs_stat_start,                \
					      &_vfs_stat_stop));               \
		} else {                                                       \
			call;                                                  \
			busy;                                                  \
		}                                                              \
	} while (0)

#endif /* FSAL_VFS_STATS_H */
