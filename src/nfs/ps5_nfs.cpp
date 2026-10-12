/*
 * SwanStationPS5 - NFS library shares: the C API the C frontend calls (ps5-native-app-boilerplate
 * based). Copyright (C) 2026 BlackBearReloaded The work (libnfs, the /nfs/<host>/<path> tree, the
 * __wrap_* file calls) is swordpdf's OrbisNfs (PS5SX2), portions copyright (C) 2026 swordpdf, under
 * src/nfs/orbis_nfs.cpp.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ps5_nfs.h"

#include "orbis_nfs.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" bool ps5_nfs_set_shares(const char *list, char *error, size_t error_size)
{
    if (error && error_size)
        error[0] = '\0';

    std::vector<std::string> problems;
    OrbisNfs::SetShares(list ? list : "", &problems);

    if (!problems.empty() && error && error_size)
    {
        std::string joined;
        for (size_t i = 0; i < problems.size(); ++i)
        {
            if (i)
                joined += "; ";
            joined += problems[i];
        }
        std::snprintf(error, error_size, "%s", joined.c_str());
    }
    return problems.empty();
}

extern "C" int ps5_nfs_mount_all(int timeout_ms)
{
    int mounted = 0;
    for (const OrbisNfs::ShareInfo &share : OrbisNfs::MountAll(timeout_ms))
        if (share.mounted)
            ++mounted;
    return mounted;
}

extern "C" int ps5_nfs_mount_points(char out[][PS5_NFS_PATH_MAX], int max)
{
    if (!out || max <= 0)
        return 0;
    int count = 0;
    for (const OrbisNfs::ShareInfo &share : OrbisNfs::Shares())
    {
        if (!share.mounted || count >= max)
            continue;
        std::snprintf(out[count], PS5_NFS_PATH_MAX, "%s", share.mount_point.c_str());
        ++count;
    }
    return count;
}

extern "C" void ps5_nfs_describe(char *out, size_t size)
{
    if (!out || size == 0)
        return;
    out[0] = '\0';
    std::string text;
    for (const OrbisNfs::ShareInfo &share : OrbisNfs::Shares())
    {
        if (!text.empty())
            text += "\n";
        text += share.mount_point;
        text += ": ";
        text += share.state;
    }
    std::snprintf(out, size, "%s", text.c_str());
}
