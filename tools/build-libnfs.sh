#!/usr/bin/env bash
# SwanStationPS5 - builds libnfs (NFS client library, LGPL-2.1+/BSD-2) as a
# static library for the PS5 app's NFS library shares (src/nfs); ps5-native-app-boilerplate based.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   tools/build-libnfs.sh   -> build/libnfs-ps5/liblibnfs.a
#
# The source and the console configuration are swordpdf's vendored copy from
# PS5SX2 (ps5/third_party/libnfs, libnfs 18.0.0); config.h picks config_ps5.h
# under __PROSPERO__. Only the five source folders libnfs needs are built.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
src="$root/third_party/libnfs"
out="$root/build/libnfs-ps5"
jobs=${BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}

[[ -f $src/include/nfsc/libnfs.h ]] || {
    echo "libnfs source missing at $src" >&2
    exit 2
}

sdk="${PS5_PAYLOAD_SDK:-$root/.deps/native/ps5-payload-sdk}"
[[ -d $sdk ]] || bash "$root/tools/setup-native-dependencies.sh" >/dev/null
export PS5_PAYLOAD_SDK=$sdk USE_CCACHE=0
cc=(sh "$root/tooling/prospero-clang18")
ar=$(command -v llvm-ar-18 || command -v llvm-ar)

flags=(-O2 -fPIC -ffunction-sections -fdata-sections -DNDEBUG -w
    -DHAVE_CONFIG_H -DHAVE_NFS4_2 "-D_U_=__attribute__((unused))"
    -I"$src" -I"$src/include" -I"$src/include/nfsc"
    -I"$src/mount" -I"$src/nfs" -I"$src/nfs4" -I"$src/portmap")

mapfile -t sources < <(cd "$src" && find lib mount nfs nfs4 portmap -name '*.c' | sort)
mkdir -p "$out/obj"
objects=()
for s in "${sources[@]}"; do
    o="$out/obj/$(echo "$s" | tr '/' '_' | sed 's/\.c$/.o/')"
    if [[ ! -f $o || $src/$s -nt $o ]]; then
        "${cc[@]}" "${flags[@]}" -std=gnu99 -c "$src/$s" -o "$o"
    fi
    objects+=("$o")
done
rm -f "$out/liblibnfs.a"
"$ar" rcs "$out/liblibnfs.a" "${objects[@]}"
echo "==> [libnfs] $out/liblibnfs.a (${#objects[@]} objects)"
