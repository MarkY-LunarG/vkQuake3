#!/usr/bin/env bash
# Build vkQuake3 with the GNU Makefile. The build goes into
# "build/<type>-<platform>-<arch>", for example "build/release-linux-x86_64".
#
# Usage: build_vkquake3.sh [-c] [-w] [-s] [-t <type>] [-j <jobs>] [-- <make variables>]
#
#   -c          Remove the build directory of this build type before the build.
#   -w          Also build the WebGPU renderer (BUILD_RENDERER_WEBGPU=1). If Dawn is
#               not in build/dawn-install, run scripts/build_dawn.sh first.
#   -s          Also build the Vulkan SC renderer (BUILD_RENDERER_VULKANSC=1). Set
#               VULKANSC_INCLUDE to the include directory of VulkanSC-Headers, or
#               VULKANSC_SDK to a directory that has it in "include".
#   -t <type>   Build type: release (default) or debug.
#   -j <jobs>   Number of parallel build jobs. The default is the CPU count.
#   -h          Show this help.
#
# On Linux, the build uses the system libraries (USE_INTERNAL_LIBS=0).
# If pkg-config does not find "opusfile", the script turns off the Ogg Opus
# codec (USE_CODEC_OPUS=0). The Quake 3 game data has no Opus files.
# Install "opusfile-devel" to keep the codec.
#
# Arguments after "--" go to make without change, for example:
#   build_vkquake3.sh -- BUILD_RENDERER_OPENGL2=0
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE="release"
CLOBBER=0
WEBGPU=0
VULKANSC=0
JOBS=""

usage()
{
	sed -n '2,23s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

while getopts ":cwst:j:h" opt; do
	case "${opt}" in
		c) CLOBBER=1 ;;
		w) WEBGPU=1 ;;
		s) VULKANSC=1 ;;
		t) BUILD_TYPE="${OPTARG}" ;;
		j) JOBS="${OPTARG}" ;;
		h) usage; exit 0 ;;
		:) echo "Option -${OPTARG} needs a value." >&2; usage >&2; exit 2 ;;
		*) echo "Unknown option: -${OPTARG}" >&2; usage >&2; exit 2 ;;
	esac
done
shift $((OPTIND - 1))
if [[ "${1:-}" == "--" ]]; then
	shift
fi

case "${BUILD_TYPE}" in
	release|debug) ;;
	*) echo "Unknown build type: ${BUILD_TYPE}. Use release or debug." >&2; exit 2 ;;
esac

if [[ -z "${JOBS}" ]]; then
	JOBS="$(nproc 2>/dev/null || echo 4)"
fi

PLATFORM="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"
BUILD_DIR="${SRC_DIR}/build/${BUILD_TYPE}-${PLATFORM}-${ARCH}"

MAKE_VARS=()
if [[ "${PLATFORM}" == "linux" ]]; then
	MAKE_VARS+=(USE_INTERNAL_LIBS=0)

	missing=()
	for module in sdl2 ogg vorbisfile zlib libjpeg openal; do
		if ! pkg-config --exists "${module}"; then
			missing+=("${module}")
		fi
	done
	if (( ${#missing[@]} > 0 )); then
		echo "pkg-config does not find: ${missing[*]}" >&2
		echo "On Fedora: sudo dnf install sdl2-compat-devel libogg-devel libvorbis-devel" \
			"zlib-ng-compat-devel libjpeg-turbo-devel openal-soft-devel" >&2
		exit 1
	fi

	if ! pkg-config --exists opusfile; then
		echo "warning: pkg-config does not find opusfile. The Ogg Opus codec is off." >&2
		echo "warning: Install opusfile-devel to build it." >&2
		MAKE_VARS+=(USE_CODEC_OPUS=0)
		# VoIP still needs libopus. Without opusfile, the Makefile also
		# adds -lopusfile to the link, so give it the opus flags only.
		MAKE_VARS+=("OPUS_CFLAGS=$(pkg-config --cflags opus)")
		MAKE_VARS+=("OPUS_LIBS=$(pkg-config --libs opus)")
	fi
fi

if (( CLOBBER )); then
	echo "Removing ${BUILD_DIR}"
	rm -rf "${BUILD_DIR}"
fi

if (( WEBGPU )); then
	if ! ls "${SRC_DIR}"/build/dawn-install/lib*/libwebgpu_dawn.so > /dev/null 2>&1; then
		echo "Dawn is not built. Running scripts/build_dawn.sh"
		"${SRC_DIR}/scripts/build_dawn.sh" -j "${JOBS}"
	fi
	MAKE_VARS+=(BUILD_RENDERER_WEBGPU=1)
fi

if (( VULKANSC )); then
	if [[ -z "${VULKANSC_INCLUDE:-}" && -n "${VULKANSC_SDK:-}" ]]; then
		VULKANSC_INCLUDE="${VULKANSC_SDK}/include"
	fi
	if [[ ! -f "${VULKANSC_INCLUDE:-}/vulkan/vulkan_sc.h" ]]; then
		echo "-s needs the Vulkan SC headers: set VULKANSC_INCLUDE to the include directory of" \
			"VulkanSC-Headers (it has vulkan/vulkan_sc.h)." >&2
		exit 1
	fi
	MAKE_VARS+=(BUILD_RENDERER_VULKANSC=1 "VULKANSC_INCLUDE=${VULKANSC_INCLUDE}")
fi

# The shared renderer code must use a graphics API only through gapi.h.
python3 -I "${SRC_DIR}/scripts/check_gapi_layering.py"

make -C "${SRC_DIR}" -j "${JOBS}" "${BUILD_TYPE}" "${MAKE_VARS[@]}" "$@"

echo
echo "Build output: ${BUILD_DIR}"
