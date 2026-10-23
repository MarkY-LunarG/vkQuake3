#!/usr/bin/env bash
# Build Dawn, the WebGPU implementation that the WebGPU renderer uses, and
# install it into "build/dawn-install". The Makefile finds it there
# (DAWN_DIR). The build is out of tree, so the Dawn source does not change.
#
# Usage: build_dawn.sh [-s <dir>] [-t <type>] [-j <jobs>] [-a <arch>] [-c]
#
#   -s <dir>    An existing Dawn source tree at the pinned commit. The default
#               is "build/dawn-src", which the script clones if it is missing.
#   -t <type>   CMake build type: Release (default), RelWithDebInfo, or Debug.
#   -j <jobs>   Number of parallel build jobs. The default is the CPU count.
#   -a <arch>   macOS only: the architecture, x86_64 (default) or arm64.
#               It must be the same as the game. The Makefile and the SDL
#               library in code/libs/macosx support only x86_64.
#   -c          Remove the Dawn build directory before the build.
#   -h          Show this help.
#
# The pinned commit is the same as in the Warzone 2100 WebGPU work. On Linux,
# Dawn uses Vulkan, with Wayland and X11 surfaces. The tint executable is
# installed too: graphics_api/shaders/glsl/compile.sh uses it to translate
# the GLSL shaders to WGSL.
#
# On macOS, Dawn uses Metal. Xcode or the Command Line Tools, CMake, and Ninja
# are necessary. On an Apple silicon Mac, the x86_64 game and Dawn run with
# Rosetta.
#
# On Windows, run the script in Git Bash or MSYS2. Dawn uses D3D12. The
# script builds Dawn with MSVC, not with MinGW. Visual Studio 2022
# with the C++ workload and the Windows SDK is necessary. If cl.exe is in
# PATH (a Visual Studio developer prompt), the build uses Ninja. If not, it
# uses the Visual Studio generator. The MinGW build of the game links to
# webgpu_dawn.dll, which has a C interface.
set -euo pipefail

DAWN_COMMIT="224dae66485fc686892222e55dae5c83821f4d10"
DAWN_URL="https://dawn.googlesource.com/dawn"

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DAWN_SRC="${SRC_DIR}/build/dawn-src"
BUILD_TYPE="Release"
JOBS=""
CLOBBER=0
OSX_ARCH="x86_64"

usage()
{
	sed -n '2,35s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

while getopts ":s:t:j:a:ch" opt; do
	case "${opt}" in
		s) DAWN_SRC="$(cd "${OPTARG}" && pwd)" ;;
		t) BUILD_TYPE="${OPTARG}" ;;
		j) JOBS="${OPTARG}" ;;
		a) OSX_ARCH="${OPTARG}" ;;
		c) CLOBBER=1 ;;
		h) usage; exit 0 ;;
		:) echo "Option -${OPTARG} needs a value." >&2; usage >&2; exit 2 ;;
		*) echo "Unknown option: -${OPTARG}" >&2; usage >&2; exit 2 ;;
	esac
done

case "$(uname -s)" in
	Linux) HOST="linux" ;;
	Darwin) HOST="macos" ;;
	MINGW*|MSYS*|CYGWIN*) HOST="windows" ;;
	*) echo "This script does not support $(uname -s)." >&2; exit 1 ;;
esac

if [[ -z "${JOBS}" ]]; then
	JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
fi

BUILD_DIR="${SRC_DIR}/build/dawn-build"
INSTALL_DIR="${SRC_DIR}/build/dawn-install"

# The Visual Studio generator does not need Ninja.
GENERATOR="Ninja"
if [[ "${HOST}" == "windows" ]] && ! command -v cl > /dev/null; then
	GENERATOR="Visual Studio 17 2022"
fi

tools=(git cmake)
if [[ "${GENERATOR}" == "Ninja" ]]; then
	tools+=(ninja)
fi
for tool in "${tools[@]}"; do
	if ! command -v "${tool}" > /dev/null; then
		echo "${tool} is necessary to build Dawn." >&2
		exit 1
	fi
done

if [[ "${HOST}" == "linux" ]]; then
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

# The window systems of each platform. Metal and D3D12 are on by default on
# macOS and Windows.
platform_options=()
case "${HOST}" in
	linux) platform_options+=(-DDAWN_USE_WAYLAND=ON -DDAWN_USE_X11=ON) ;;
	macos) platform_options+=(-DDAWN_USE_WAYLAND=OFF -DDAWN_USE_X11=OFF -DCMAKE_OSX_ARCHITECTURES="${OSX_ARCH}") ;;
	windows) platform_options+=(-DDAWN_USE_WAYLAND=OFF -DDAWN_USE_X11=OFF) ;;
esac

generator_options=(-G "${GENERATOR}")
if [[ "${GENERATOR}" == "Visual Studio 17 2022" ]]; then
	generator_options+=(-A x64)
elif [[ "${HOST}" == "windows" ]]; then
	# MSYS2 can have gcc in PATH before cl.
	generator_options+=(-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl)
fi

cmake -S "${DAWN_SRC}" -B "${BUILD_DIR}" "${generator_options[@]}" \
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
	-DDAWN_ENABLE_DESKTOP_GL=OFF \
	-DDAWN_ENABLE_OPENGLES=OFF \
	"${platform_options[@]}"

# The Visual Studio generator ignores CMAKE_BUILD_TYPE and uses --config.
cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" -j "${JOBS}"
cmake --install "${BUILD_DIR}" --config "${BUILD_TYPE}"

# cmake --install does not install the tint executable. The Visual Studio
# generator puts it in a directory for each configuration.
mkdir -p "${INSTALL_DIR}/bin"
tint="$(find "${BUILD_DIR}" -maxdepth 2 -type f \( -name tint -o -name tint.exe \) | head -n 1)"
if [[ -n "${tint}" ]]; then
	cp "${tint}" "${INSTALL_DIR}/bin/"
else
	echo "Warning: the tint executable is not in ${BUILD_DIR}." >&2
fi

case "${HOST}" in
	macos)
		# The game finds the library with its rpath (@loader_path).
		install_name_tool -id "@rpath/libwebgpu_dawn.dylib" "${INSTALL_DIR}/lib/libwebgpu_dawn.dylib"
		;;
	windows)
		# The DLL must be in bin, next to the DXC compiler DLLs if Dawn has them.
		if [[ ! -e "${INSTALL_DIR}/bin/webgpu_dawn.dll" ]]; then
			dll="$(find "${BUILD_DIR}" -type f -name webgpu_dawn.dll | head -n 1)"
			[[ -n "${dll}" ]] && cp "${dll}" "${INSTALL_DIR}/bin/"
		fi
		;;
esac

echo
echo "Dawn is installed in ${INSTALL_DIR}"
