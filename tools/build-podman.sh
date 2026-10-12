#!/usr/bin/env bash
# Build the Vulkan app folder with the pinned RADV sources in Podman.
# Run from anywhere: bash tools/build-podman.sh
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
image=${PS5_BUILD_IMAGE:-localhost/ps5-retroarch-build:clang18}
mesa_rev=7b59ef27c1b09b9671bc4153c41940c3155c3af2
radv_tag=deps-radv-${mesa_rev:0:8}

if [[ ${1:-} != --inside ]]; then
    [[ $# -eq 0 ]] || { echo "usage: $0" >&2; exit 2; }
    command -v podman >/dev/null || { echo "Podman is required" >&2; exit 2; }
    if [[ -z ${PS5_BUILD_IMAGE:-} ]] && ! podman image exists "$image"; then
        image=ubuntu:24.04
    fi
    exec podman run --rm -v "$root:/src:z" -w /src "$image" bash tools/build-podman.sh --inside
fi

[[ $root == /src ]] || { echo "Run --inside only in the /src Podman mount" >&2; exit 2; }
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq clang-18 lld-18 llvm-18 llvm-18-dev make git curl wget \
    unzip tar file patch python3 perl ccache rsync pkg-config ninja-build zstd \
    libclang-rt-18-dev >/dev/null
export PATH="/usr/lib/llvm-18/bin:$PATH"
export PS5_CLANG=/usr/bin/clang-18
export LLVM_OBJCOPY="$(command -v llvm-objcopy)"

git submodule update --init --recursive third_party/rcheevos third_party/swanstation
bash tools/setup-native-dependencies.sh >/dev/null
native="$root/.deps/native"
if [[ ! -f $native/radv-release/lib/libvulkan_radeon.ps5.a ||
      ! -f $native/radv-release/.tag || $(<"$native/radv-release/.tag") != "$radv_tag" ||
      ! -f $native/ps5-payload-sdk/target/lib/libps5platform.a ]]; then
    apt-get install -y -qq libclang-18-dev libclang-cpp18-dev libclc-18-dev \
        libllvmspirvlib-18-dev spirv-tools glslang-tools bison flex rsync \
        pkg-config python3-mako python3-yaml python3-packaging python3-venv >/dev/null
    sources="$root/.deps/radv-source"
    mkdir -p "$sources"
    for spec in \
        "PS5_Vulkan fde9e3798c2ec1d8fccc90d5067cd6060490607f" \
        "PS5_Mesa $mesa_rev" \
        "PS5_PayloadSDK b83202be73e930050e00de0f1b4d9ec46c0391bf"; do
        read -r name rev <<< "$spec"
        if [[ ! -d $sources/$name/.git ]]; then
            git clone -q --filter=blob:none "https://github.com/mihawk-99/$name.git" "$sources/$name"
        fi
        git -C "$sources/$name" cat-file -e "$rev^{commit}" || git -C "$sources/$name" fetch origin "$rev"
        git -C "$sources/$name" checkout -q "$rev"
    done
    venv="$sources/meson-venv"
    [[ -x $venv/bin/meson ]] || {
        python3 -m venv "$venv"
        "$venv/bin/pip" install 'meson>=1.4'
    }
    export PS5_PAYLOAD_SDK_FORK="$sources/PS5_PayloadSDK"
    export PS5_MESA_FORK="$sources/PS5_Mesa"
    export MESON="$venv/bin/meson"
    export BUILD_JOBS=${BUILD_JOBS:-12}
    bash "$sources/PS5_Vulkan/tools/setup-native-dependencies.sh"
    bash "$sources/PS5_Vulkan/tools/build-radv.sh" release

    from="$sources/PS5_Vulkan/.deps/native"
    mkdir -p "$native" "$native/ps5-payload-sdk/target/lib" "$native/ps5-payload-sdk/target/include"
    rm -rf "$native/radv-release"
    cp -a "$from/radv-release" "$native/"
    cp "$from/ps5-payload-sdk/target/lib/libps5platform.a" "$native/ps5-payload-sdk/target/lib/"
    rm -rf "$native/ps5-payload-sdk/target/include/ps5platform"
    cp -a "$from/ps5-payload-sdk/target/include/ps5platform" "$native/ps5-payload-sdk/target/include/"
    printf '%s\n' "$radv_tag" > "$native/radv-release/.tag"
fi

make -j2
python3 tools/check-imports.py dist/PPSA98510/eboot.bin
printf '\nApp folder: %s/dist/PPSA98510\n' "$root"
