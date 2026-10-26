#!/usr/bin/env bash
# Builds OpenTalk and produces a portable ZIP for Linux.
# The archive carries the binaries and any non-system shared libraries Qt was
# linked against; system libraries (glibc, fontconfig) are expected on the host.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/build"
dist_dir="${repo_root}/dist"
stage_dir="${dist_dir}/stage"
portable_dir="${dist_dir}/portable"
version="0.1.0"
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
# The SDK is a shared library installed to lib/; the binaries expect it next
# to themselves via the $ORIGIN rpath.
if [[ -d "${stage_dir}/lib" ]]; then
    cp -a "${stage_dir}/lib/." "${portable_dir}/"
fi
cp "${repo_root}/LICENSE" "${repo_root}/README.md" "${portable_dir}/"

# Bundle the Qt libraries the binaries need, resolved from the linked rpath.
if command -v ldd >/dev/null 2>&1; then
    for binary in "${stage_dir}"/bin/OpenTalkClient "${stage_dir}"/bin/OpenTalkServer; do
        [[ -f "${binary}" ]] || continue
        ldd "${binary}" | awk '/=> \//{print $3}' | while read -r lib; do
            case "$(basename "${lib}")" in
                libQt6*|libOpenTalk*|libicu*) cp -Ln "${lib}" "${portable_dir}/" 2>/dev/null || true ;;
            esac
        done
    done
fi

# Bundle the Qt platform/image/tls plugins. Without them the GUI exits at
# startup with "could not load the Qt platform plugin".
qt_prefix=""
if command -v ldd >/dev/null 2>&1 && [[ -f "${stage_dir}/bin/OpenTalkClient" ]]; then
    qt_core="$(ldd "${stage_dir}/bin/OpenTalkClient" 2>/dev/null \
        | awk '/libQt6Core\.so/{print $3; exit}')"
    if [[ -n "${qt_core}" ]]; then
        qt_prefix="$(cd "$(dirname "${qt_core}")/.." && pwd)"
    fi
fi

if [[ -n "${qt_prefix}" && -d "${qt_prefix}/plugins" ]]; then
    for group in platforms imageformats iconengines tls; do
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
