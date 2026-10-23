#!/usr/bin/env bash
# Build Dawn, the WebGPU implementation that the WebGPU renderer uses, and
# install it into "build/dawn-install". The Makefile finds it there
# (DAWN_DIR). The build is out of tree, so the Dawn source does not change.
#
# Usage: build_dawn.sh [-s <dir>] [-t <type>] [-j <jobs>] [-c]
#
#   -s <dir>    An existing Dawn source tree at the pinned commit. The default
#               is "build/dawn-src", which the script clones if it is missing.
#   -t <type>   CMake build type: Release (default), RelWithDebInfo, or Debug.
#   -j <jobs>   Number of parallel build jobs. The default is the CPU count.
#   -c          Remove the Dawn build directory before the build.
#   -h          Show this help.
#
# The pinned commit is the same as in the Warzone 2100 WebGPU work. On Linux,
# Dawn uses Vulkan, with Wayland and X11 surfaces. The tint executable is
# installed too: graphics_api/shaders/glsl/compile.sh uses it to translate
# the GLSL shaders to WGSL.
set -euo pipefail

DAWN_COMMIT="224dae66485fc686892222e55dae5c83821f4d10"
DAWN_URL="https://dawn.googlesource.com/dawn"

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DAWN_SRC="${SRC_DIR}/build/dawn-src"
BUILD_TYPE="Release"
JOBS=""
CLOBBER=0

usage()
{
	sed -n '2,19s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

while getopts ":s:t:j:ch" opt; do
	case "${opt}" in
		s) DAWN_SRC="$(cd "${OPTARG}" && pwd)" ;;
		t) BUILD_TYPE="${OPTARG}" ;;
		j) JOBS="${OPTARG}" ;;
		c) CLOBBER=1 ;;
		h) usage; exit 0 ;;
		:) echo "Option -${OPTARG} needs a value." >&2; usage >&2; exit 2 ;;
		*) echo "Unknown option: -${OPTARG}" >&2; usage >&2; exit 2 ;;
	esac
done

if [[ -z "${JOBS}" ]]; then
	JOBS="$(nproc 2>/dev/null || echo 4)"
fi

BUILD_DIR="${SRC_DIR}/build/dawn-build"
INSTALL_DIR="${SRC_DIR}/build/dawn-install"

for tool in git cmake ninja; do
	if ! command -v "${tool}" > /dev/null; then
		echo "${tool} is necessary to build Dawn." >&2
		exit 1
	fi
done

if [[ "$(uname -s)" == "Linux" ]]; then
	missing=()
	for module in wayland-client wayland-cursor wayland-egl xkbcommon x11 x11-xcb xcb xrandr; do
		if ! pkg-config --exists "${module}"; then
			missing+=("${module}")
		fi
	done
	if (( ${#missing[@]} > 0 )); then
		echo "Dawn needs the Wayland and X11 development packages. Missing: ${missing[*]}" >&2
		exit 1
	fi
fi

if [[ ! -d "${DAWN_SRC}/.git" ]]; then
	echo "Cloning Dawn ${DAWN_COMMIT} into ${DAWN_SRC}"
	git init -q "${DAWN_SRC}"
	git -C "${DAWN_SRC}" fetch --depth 1 "${DAWN_URL}" "${DAWN_COMMIT}"
	git -C "${DAWN_SRC}" checkout -q FETCH_HEAD
fi

actual="$(git -C "${DAWN_SRC}" rev-parse HEAD)"
if [[ "${actual}" != "${DAWN_COMMIT}" ]]; then
	echo "${DAWN_SRC} is at ${actual}, not at the pinned commit ${DAWN_COMMIT}." >&2
	exit 1
fi

if (( CLOBBER )); then
	rm -rf "${BUILD_DIR}"
fi

cmake -S "${DAWN_SRC}" -B "${BUILD_DIR}" -G Ninja \
	-DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
	-DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
	-DDAWN_ENABLE_INSTALL=ON \
	-DDAWN_BUILD_MONOLITHIC_LIBRARY=SHARED \
	-DDAWN_FETCH_DEPENDENCIES=ON \
	-DDAWN_USE_GLFW=OFF \
	-DDAWN_BUILD_SAMPLES=OFF \
	-DDAWN_BUILD_TESTS=OFF \
	-DDAWN_BUILD_PROTOBUF=OFF \
	-DTINT_BUILD_TESTS=OFF \
	-DTINT_BUILD_IR_BINARY=OFF \
	-DTINT_BUILD_CMD_TOOLS=ON \
	-DDAWN_USE_WAYLAND=ON \
	-DDAWN_USE_X11=ON \
	-DDAWN_ENABLE_DESKTOP_GL=OFF \
	-DDAWN_ENABLE_OPENGLES=OFF

cmake --build "${BUILD_DIR}" -j "${JOBS}"
cmake --install "${BUILD_DIR}"

# cmake --install does not install the tint executable.
mkdir -p "${INSTALL_DIR}/bin"
cp "${BUILD_DIR}/tint" "${INSTALL_DIR}/bin/tint"

echo
echo "Dawn is installed in ${INSTALL_DIR}"
