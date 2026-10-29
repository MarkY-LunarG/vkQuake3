#!/usr/bin/env bash
# Run the vkQuake3 build from this repository with the game data of a Quake 3
# Arena installation. The game reads the data from that installation and
# does not change it. It keeps its config, screenshots, demos, and videos in
# "build_userdata" in the repository, not in ~/.q3a.
#
# Usage: run_vkquake3.sh [-r <renderer>] [-t <type>] [-d <dir>] [-u <dir>] [-- <game args>]
#
#   -r <renderer> Renderer library: vulkan (default), webgpu, vulkansc, opengl1, or opengl2.
#                 webgpu needs a build with build_vkquake3.sh -w, and vulkansc a
#                 build with build_vkquake3.sh -s. vulkansc also needs the Vulkan SC
#                 environment (VK_DRIVER_FILES, QVKSC_LIBRARY, QVKSC_PIPELINE_DIR).
#                 The game loads "renderer_<renderer>_<arch>.so".
#   -t <type>     Build type: release (default) or debug.
#   -d <dir>      Quake 3 Arena installation, the directory that holds "baseq3".
#                 The default is $Q3_DATA, or the Steam installation in
#                 ~/.local/share/Steam/steamapps/common/Quake 3 Arena.
#   -u <dir>      User data directory (fs_homepath). The default is "build_userdata".
#   -h            Show this help.
#
# Arguments after "--" go to the game without change, for example:
#   run_vkquake3.sh -- +set r_mode 4 +devmap q3dm17
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RENDERER="vulkan"
BUILD_TYPE="release"
DATA_DIR="${Q3_DATA:-${HOME}/.local/share/Steam/steamapps/common/Quake 3 Arena}"
USER_DIR="${SRC_DIR}/build_userdata"

usage()
{
	sed -n '2,22s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

while getopts ":r:t:d:u:h" opt; do
	case "${opt}" in
		r) RENDERER="${OPTARG}" ;;
		t) BUILD_TYPE="${OPTARG}" ;;
		d) DATA_DIR="${OPTARG}" ;;
		u) USER_DIR="${OPTARG}" ;;
		h) usage; exit 0 ;;
		:) echo "Option -${OPTARG} needs a value." >&2; usage >&2; exit 2 ;;
		*) echo "Unknown option: -${OPTARG}" >&2; usage >&2; exit 2 ;;
	esac
done
shift $((OPTIND - 1))
if [[ "${1:-}" == "--" ]]; then
	shift
fi

PLATFORM="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"
BUILD_DIR="${SRC_DIR}/build/${BUILD_TYPE}-${PLATFORM}-${ARCH}"
CLIENT="${BUILD_DIR}/ioquake3.${ARCH}"

if [[ ! -x "${CLIENT}" ]]; then
	echo "The client is not built: ${CLIENT}" >&2
	echo "Run: ./build_vkquake3.sh -t ${BUILD_TYPE}" >&2
	exit 1
fi
if [[ ! -f "${BUILD_DIR}/renderer_${RENDERER}_${ARCH}.so" ]]; then
	echo "The renderer is not built: ${BUILD_DIR}/renderer_${RENDERER}_${ARCH}.so" >&2
	exit 1
fi
if [[ ! -f "${DATA_DIR}/baseq3/pak0.pk3" ]]; then
	echo "No Quake 3 game data in: ${DATA_DIR}" >&2
	echo "Use -d <dir> or set Q3_DATA to the directory that holds baseq3/pak0.pk3." >&2
	exit 1
fi

# The game reads q3config.cfg from fs_homepath first. An empty file there
# keeps the game from reading the q3config.cfg of the installation.
mkdir -p "${USER_DIR}/baseq3"
if [[ ! -f "${USER_DIR}/baseq3/q3config.cfg" ]]; then
	: > "${USER_DIR}/baseq3/q3config.cfg"
fi

exec "${CLIENT}" \
	+set fs_basepath "${DATA_DIR}" \
	+set fs_homepath "${USER_DIR}" \
	+set cl_renderer "${RENDERER}" \
	"$@"
