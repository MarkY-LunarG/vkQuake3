#!/usr/bin/env bash
# Make the pipeline cache of the Vulkan SC renderer. The script runs the
# export sessions with the renderer in export mode (QVKSC_EXPORT_PIPELINES),
# and then the pipeline cache compiler (PCC) of the Vulkan SC implementation.
# The output directory gets q3.vksc.bin and q3_pipelines.txt, which the
# renderer reads from QVKSC_PIPELINE_DIR, or from <fs_homepath>/vulkansc.
#
# Usage: vulkansc_pipelines.sh -o <dir> [-t <type>] [-d <dir>] [-m <maps>] [-k]
#
#   -o <dir>    The output directory.
#   -t <type>   Build type of the game: release (default) or debug.
#   -d <dir>    Quake 3 Arena installation, the directory that holds "baseq3".
#               The default is $Q3_DATA, or the Steam installation.
#   -m <maps>   The maps to load, separated by spaces. The default is every
#               map in the paks of baseq3.
#   -k          Keep the JSON documents and the SPIR-V in <dir>/json.
#   -h          Show this help.
#
# Environment:
#   VK_DRIVER_FILES   The Vulkan SC ICD, for example the vksconvk.json of
#                     VulkanSC-Emulation.
#   QVKSC_LIBRARY     The Vulkan SC loader, if it is not installed.
#   QVKSC_PCC         The PCC. The default is pcconvk in PATH.
#   QVKSC_PCC_ARGS    The PCC options before the output file name. The
#                     default is "--path <json dir> --prefix q3.pc --out", the
#                     options of pcconvk.
#
# Each export session is one short run of the game: one map, loaded for a
# few frames, then "quit". The last session plays the first 300 frames of
# "demo four". The front end makes the pipelines of a map when the map loads
# its shaders, so the sessions draw nothing.
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR=""
BUILD_TYPE="release"
DATA_DIR="${Q3_DATA:-${HOME}/.local/share/Steam/steamapps/common/Quake 3 Arena}"
MAPS=""
KEEP=0

usage()
{
	sed -n '2,31s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

while getopts ":o:t:d:m:kh" opt; do
	case "${opt}" in
		o) OUT_DIR="${OPTARG}" ;;
		t) BUILD_TYPE="${OPTARG}" ;;
		d) DATA_DIR="${OPTARG}" ;;
		m) MAPS="${OPTARG}" ;;
		k) KEEP=1 ;;
		h) usage; exit 0 ;;
		:) echo "Option -${OPTARG} needs a value." >&2; usage >&2; exit 2 ;;
		*) echo "Unknown option: -${OPTARG}" >&2; usage >&2; exit 2 ;;
	esac
done

if [[ -z "${OUT_DIR}" ]]; then
	echo "Give the output directory with -o." >&2
	usage >&2
	exit 2
fi
mkdir -p "${OUT_DIR}"
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"

PCC="${QVKSC_PCC:-$(command -v pcconvk || true)}"
if [[ -z "${PCC}" ]]; then
	echo "No PCC: set QVKSC_PCC, or put pcconvk in PATH." >&2
	exit 1
fi

if [[ -z "${MAPS}" ]]; then
	for pak in "${DATA_DIR}"/baseq3/pak*.pk3; do
		unzip -Z1 "${pak}" 'maps/*.bsp' 2>/dev/null || true
	done > "${OUT_DIR}/maps.txt"
	MAPS="$(sed -e 's|^maps/||' -e 's|\.bsp$||' "${OUT_DIR}/maps.txt" | sort -u | tr '\n' ' ')"
	rm -f "${OUT_DIR}/maps.txt"
fi

JSON_DIR="${OUT_DIR}/json"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT
rm -rf "${JSON_DIR}"
mkdir -p "${JSON_DIR}"

# run_session <name> <game arguments...>
run_session()
{
	local name="$1"
	shift
	if ! QVKSC_EXPORT_PIPELINES="${JSON_DIR}" timeout 120 "${SRC_DIR}/run_vkquake3.sh" -r vulkansc \
		-t "${BUILD_TYPE}" -d "${DATA_DIR}" -u "${WORK_DIR}/user" -- "$@" +quit \
		> "${WORK_DIR}/${name}.log" 2>&1; then
		echo "The export session ${name} failed. Its log:" >&2
		tail -20 "${WORK_DIR}/${name}.log" >&2
		exit 1
	fi
	echo "  ${name}: $(wc -l < "${JSON_DIR}/q3_pipelines.txt") pipelines in all"
}

echo "Export sessions:"
for map in ${MAPS}; do
	run_session "${map}" +map "${map}" +wait 20
done
run_session "demo_four" +demo four +wait 300

echo "Building the pipeline cache with ${PCC}"
# shellcheck disable=SC2086
"${PCC}" ${QVKSC_PCC_ARGS:---path "${JSON_DIR}" --prefix q3.pc --out} "${OUT_DIR}/q3.vksc.bin"
cp "${JSON_DIR}/q3_pipelines.txt" "${OUT_DIR}/"
if [[ ${KEEP} -eq 0 ]]; then
	rm -rf "${JSON_DIR}"
fi
echo "Wrote ${OUT_DIR}/q3.vksc.bin and ${OUT_DIR}/q3_pipelines.txt ($(wc -l < "${OUT_DIR}/q3_pipelines.txt") pipelines)"
