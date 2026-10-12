/*
 * SwanStationPS5 - NFS library shares, C API for the frontend (ps5-native-app-boilerplate based).
 * Copyright (C) 2026 BlackBearReloaded
 * The shim it drives is ported from PS5SX2's OrbisNfs; portions copyright (C) 2026 swordpdf.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Games can live on an NFS server instead of a local drive. The console kernel
 * has no NFS client, so the app is its own (libnfs, third_party/libnfs) and
 * serves the shares under /nfs/<host>/<path> by wrapping the C library's file
 * calls (src/nfs/orbis_nfs.cpp). Only this process can read them.
 *
 * A share is an nfs:// address, several separated by ';' or new lines, stored
 * as nfs_shares in SwanStationPS5.ini:
 *   nfs://192.168.1.187/shared/array            NFS v3 via the portmapper (Linux, NAS), else v4 on
 * 2049 nfs://192.168.1.187/srv/games?version=4     NFS v4 only
 *   nfs://nas/psx?nfsport=2049&mountport=1058   fixed ports (no portmapper); also uid=, gid=
 * The share's files appear under /nfs/<host>/<path> and the shelf lists its
 * games like a drive's. Read-only: writing on a share fails with EROFS.
 */
#ifndef SwanStationPS5_NFS_PS5_NFS_H
#define SwanStationPS5_NFS_PS5_NFS_H

#include <stdbool.h>
#include <stddef.h>

#define PS5_NFS_MAX_MOUNTS 8
#define PS5_NFS_PATH_MAX 512

#ifdef __cplusplus
extern "C"
{
#endif

    /* Parse a share list and remember it (replacing any set before). Addresses that
     * don't parse are left out; their reasons are written to error (when non-NULL).
     * Returns true when every address parsed. Never touches the network. */
    bool ps5_nfs_set_shares(const char *list, char *error, size_t error_size);

    /* Mount every share not mounted yet, each giving up after about timeout_ms.
     * Blocking. Returns the number of shares mounted (0 when there are none). */
    int ps5_nfs_mount_all(int timeout_ms);

    /* Fills out[0..] with the mount points (e.g. /nfs/192.168.1.187/shared/array)
     * of the shares that are mounted, up to max. Returns how many were written. */
    int ps5_nfs_mount_points(char out[][PS5_NFS_PATH_MAX], int max);

    /* A one-line description of every share and its state, for the log. */
    void ps5_nfs_describe(char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
