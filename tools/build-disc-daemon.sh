#!/usr/bin/env bash
# SwanStationPS5 - builds the disc daemon payload: dist/SwanStationPS5-DiscDaemon.elf. Send it once to the console's
# ELF loader (port 9021) and leave it running; see payload/discd.c.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk="${PS5_PAYLOAD_SDK:-$root/.deps/native/ps5-payload-sdk}"
[[ -x $sdk/bin/prospero-clang ]] || bash "$root/tools/setup-native-dependencies.sh" >/dev/null
out="$root/dist"
mkdir -p "$out"
"$sdk/bin/prospero-clang" -std=gnu11 -O2 -Wall -Wextra -DSwanStationPS5_PAYLOAD=1 \
    -I"$root/payload/shim" -I"$root/src" \
    -o "$out/SwanStationPS5-DiscDaemon.elf" \
    "$root/payload/discd.c" "$root/payload/stubs.c" \
    "$root/src/discimport.c" "$root/src/cdscsi.c" "$root/src/disc.c" "$root/src/util.c" \
    -L"$sdk/target/lib" 2>&1
ls -l "$out/SwanStationPS5-DiscDaemon.elf"
