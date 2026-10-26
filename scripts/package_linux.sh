#!/usr/bin/env bash
# Builds OpenTalk and produces a portable ZIP for Linux.
#
# The archive carries the binaries, the SDK, Opus and the Qt runtime plus
# plugins. System libraries that every desktop distribution provides
# (glibc, X11/XKB, fontconfig, freetype, PulseAudio, EGL) are NOT bundled:
# duplicating them tends to break on other distributions.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/build"
dist_dir="${repo_root}/dist"
stage_dir="${dist_dir}/stage"
portable_dir="${dist_dir}/portable"
version="1.0.0"
zip_path="${dist_dir}/OpenTalk-${version}-portable.zip"

cmake -S "${repo_root}" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "${build_dir}" -j"$(nproc 2>/dev/null || echo 2)"

if [[ "${OPENTALK_SKIP_TESTS:-0}" != "1" ]]; then
    ctest --test-dir "${build_dir}" --output-on-failure
fi

rm -rf "${stage_dir}" "${portable_dir}"
cmake --install "${build_dir}" --prefix "${stage_dir}"

mkdir -p "${portable_dir}"
cp -a "${stage_dir}/bin/." "${portable_dir}/"
# The SDK is a shared library installed to lib/; the binaries expect it next to
# themselves via the $ORIGIN rpath.
if [[ -d "${stage_dir}/lib" ]]; then
    cp -a "${stage_dir}/lib/." "${portable_dir}/"
fi
cp "${repo_root}/LICENSE" "${repo_root}/README.md" "${portable_dir}/"
cp "${repo_root}/CHANGELOG.md" "${repo_root}/CONTRIBUTING.md" \
   "${repo_root}/SECURITY.md" "${portable_dir}/"

# Documentation and SDK development files travel with the binaries so the
# archive is usable both to run OpenTalk and to build against its SDK.
mkdir -p "${portable_dir}/docs" "${portable_dir}/sdk/include"
cp -a "${repo_root}/docs/." "${portable_dir}/docs/"
cp -a "${repo_root}/sdk/include/." "${portable_dir}/sdk/include/"

# Resolve dependencies from the *build* tree. Installed binaries have an
# $ORIGIN-only rpath, so ldd on them cannot find the Qt kit or Opus, while the
# build tree still records both.
bundle_libraries() {
    local binary="$1"
    [[ -f "${binary}" ]] || return 0
    ldd "${binary}" 2>/dev/null | awk '/=> \//{print $3}' | while read -r lib; do
        case "$(basename "${lib}")" in
            libQt6*|libOpenTalk*|libicu*|libopus*)
                cp -Ln "${lib}" "${portable_dir}/" 2>/dev/null || true
                ;;
        esac
    done
}

for binary in \
    "${build_dir}/client/OpenTalkClient" \
    "${build_dir}/server/OpenTalkServer" \
    "${build_dir}"/sdk/libOpenTalkSDK.so*; do
    bundle_libraries "${binary}"
done

# Bundle the Qt platform/image/tls/multimedia plugins. Without them the GUI
# exits at startup with "could not load the Qt platform plugin".
qt_prefix=""
if command -v ldd >/dev/null 2>&1 && [[ -f "${build_dir}/client/OpenTalkClient" ]]; then
    qt_core="$(ldd "${build_dir}/client/OpenTalkClient" 2>/dev/null \
        | awk '/libQt6Core\.so/{print $3; exit}')"
    if [[ -n "${qt_core}" ]]; then
        qt_prefix="$(cd "$(dirname "${qt_core}")/.." && pwd)"
    fi
fi

if [[ -n "${qt_prefix}" && -d "${qt_prefix}/plugins" ]]; then
    for group in platforms imageformats iconengines tls multimedia; do
        if [[ -d "${qt_prefix}/plugins/${group}" ]]; then
            mkdir -p "${portable_dir}/plugins"
            cp -a "${qt_prefix}/plugins/${group}" "${portable_dir}/plugins/"
        fi
    done
    # Point Qt at the bundled plugins instead of the build machine's prefix.
    cat > "${portable_dir}/qt.conf" <<'QTCONF'
[Paths]
Plugins = plugins
QTCONF
fi

rm -f "${zip_path}"
(cd "${portable_dir}" && zip -qr "${zip_path}" .)

echo
echo "Portable build ready: ${zip_path}"
