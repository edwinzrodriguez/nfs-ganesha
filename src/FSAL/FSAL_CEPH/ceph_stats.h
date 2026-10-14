/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright (C) 2026
 *
 * Ceph FSAL operation statistics (patterned after FSAL_GPFS).
 */

#ifndef FSAL_CEPH_STATS_H
#define FSAL_CEPH_STATS_H

#include "fsal.h"
#include "common_utils.h"
#include "nfs_core.h"

/**
 * Indices for Ceph FSAL stats. Contiguous so they map 1:1 into
 * fsal_op_stats[]. Keep in sync with ceph_opcode_to_name().
 */
enum ceph_stat_op {
	CEPH_STAT_LOOKUP = 0,
	CEPH_STAT_GETATTR,
	CEPH_STAT_SETATTR,
	CEPH_STAT_CREATE,
	CEPH_STAT_OPEN,
	CEPH_STAT_CLOSE,
	CEPH_STAT_READ,
	CEPH_STAT_WRITE,
	CEPH_STAT_MKDIR,
	CEPH_STAT_MKNOD,
	CEPH_STAT_SYMLINK,
	CEPH_STAT_READLINK,
	CEPH_STAT_LINK,
	CEPH_STAT_UNLINK,
	CEPH_STAT_RMDIR,
	CEPH_STAT_RENAME,
	CEPH_STAT_OPENDIR,
	CEPH_STAT_RELEASEDIR,
	CEPH_STAT_FSYNC,
	CEPH_STAT_GETLK,
	CEPH_STAT_SETLK,
	CEPH_STAT_GETXATTR,
	CEPH_STAT_SETXATTR,
	CEPH_STAT_REMOVEXATTR,
	CEPH_STAT_FALLOCATE,
	CEPH_STAT_WALK,
	CEPH_STAT_DS_READ,
	CEPH_STAT_DS_WRITE,
	CEPH_STAT_MAX
};

extern struct fsal_stats ceph_stats;

void ceph_prepare_for_stats(struct fsal_module *fsal_hdl);
#ifdef USE_DBUS
void ceph_fsal_extract_stats(struct fsal_module *fsal_hdl, void *iter);
#endif
void ceph_fsal_reset_stats(struct fsal_module *fsal_hdl);
void ceph_fsal_record_stats(enum ceph_stat_op op, uint64_t resp_time);

/**
 * Time a libcephfs call and record FSAL stats when enabled.
 *
 * Usage:
 *   CEPH_FSAL_TIME_CALL(CEPH_STAT_LOOKUP, rc = ceph_ll_lookup(...));
 */
#define CEPH_FSAL_TIME_CALL(op, call)                                          \
	do {                                                                   \
		if (nfs_param.core_param.enable_FSALSTATS) {                   \
			struct timespec _ceph_stat_start, _ceph_stat_stop;     \
			now(&_ceph_stat_start);                                \
			call;                                                  \
			now(&_ceph_stat_stop);                                 \
			ceph_fsal_record_stats(                                \
				(op),                                          \
				timespec_diff(&_ceph_stat_start,               \
					      &_ceph_stat_stop));              \
		} else {                                                       \
			call;                                                  \
		}                                                              \
	} while (0)

#endif /* FSAL_CEPH_STATS_H */
