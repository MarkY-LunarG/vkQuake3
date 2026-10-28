#!/usr/bin/env bash
# Tests of the Vulkan SC renderer on a Vulkan SC implementation, for example
# the VulkanSC-Emulation ICD. Each test is a short run of the game.
#
# Usage: vulkansc_tests.sh -p <dir> [-t <type>] [-d <dir>] [-k] [-h]
#
#   -p <dir>    The pipeline cache directory (q3.vksc.bin and q3_pipelines.txt),
#               from scripts/vulkansc_pipelines.sh.
#   -t <type>   Build type of the game: release (default) or debug.
#   -d <dir>    Quake 3 Arena installation, the directory that holds "baseq3".
#               The default is $Q3_DATA, or the Steam installation.
#   -k          Keep the work directory, with the logs and the screenshots.
#   -h          Show this help.
#
# Environment:
#   VK_DRIVER_FILES        The Vulkan SC ICD (for example vksconvk.json).
#   QVKSC_LIBRARY          The Vulkan SC loader, if it is not installed.
#   QVKSC_VALIDATION_DIR   The directory of the desktop validation layer manifest
#                          (VkLayer_khronos_validation.json). With it, the
#                          emulation ICD runs the layer underneath
#                          (VKSC_EMU_VK_LAYER_PATH), and the tests check that it
#                          is active.
#   GFXR_VULKANSC_BUILD    A Premium-GFXR build with Vulkan SC. With it, a run is
#                          captured with the Vulkan SC capture layer, and the
#                          capture is replayed and checked with
#                          gfxrecon-premium-info. Needs QVKSC_LIBRARY: the layer
#                          forwards to the loader.
#
# The tests:
#   demo       300 frames of "demo four".
#   memory     "demo four", then q3dm15, with r_picmip 0: the most image memory.
#   scenes     The scenes of scripts/vulkansc_scenes.cfg (menu, mirror, portal,
#              fog), compared with the desktop Vulkan renderer.
#   frame      Frame 200 of "demo four" with "timedemo 1", compared with the
#              desktop Vulkan renderer.
#   capture    With GFXR_VULKANSC_BUILD: capture "demo four", replay, and check
#              the capture: no faults, no error messages, and every pipeline of
#              the pipeline cache in its pipeline cache sources.
#
# A Vulkan SC run passes when the game quits by itself with exit status 0, the
# log has no validation message, no Vulkan error, no pipeline that is missing
# from the pipeline cache, and no device memory allocation after startup.
# The image comparison passes when at most 1% of the pixels differ by more
# than 16 in a channel (scripts/compare_images.py).
set -uo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PIPELINE_DIR=""
BUILD_TYPE="release"
DATA_DIR="${Q3_DATA:-${HOME}/.local/share/Steam/steamapps/common/Quake 3 Arena}"
KEEP=0
COMPARE_THRESHOLD=16
COMPARE_LIMIT_PERCENT=1

usage()
{
	sed -n '2,44s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

while getopts ":p:t:d:kh" opt; do
	case "${opt}" in
		p) PIPELINE_DIR="${OPTARG}" ;;
		t) BUILD_TYPE="${OPTARG}" ;;
		d) DATA_DIR="${OPTARG}" ;;
		k) KEEP=1 ;;
		h) usage; exit 0 ;;
		:) echo "Option -${OPTARG} needs a value." >&2; usage >&2; exit 2 ;;
		*) echo "Unknown option: -${OPTARG}" >&2; usage >&2; exit 2 ;;
	esac
done

if [[ -z "${PIPELINE_DIR}" || ! -f "${PIPELINE_DIR}/q3.vksc.bin" ]]; then
	echo "Give the pipeline cache directory with -p (scripts/vulkansc_pipelines.sh makes it)." >&2
	exit 2
fi
PIPELINE_DIR="$(cd "${PIPELINE_DIR}" && pwd)"

WORK_DIR="$(mktemp -d)"
if [[ ${KEEP} -eq 0 ]]; then
	trap 'rm -rf "${WORK_DIR}"' EXIT
fi
FAILURES=0

# Settings that make the runs the same each time.
COMMON_ARGS=(
	+set s_initsound 0
	+set com_introplayed 1
	+set cg_lagometer 0
	+set in_mouse 0
)

SC_ENV=(QVKSC_PIPELINE_DIR="${PIPELINE_DIR}")
if [[ -n "${QVKSC_VALIDATION_DIR:-}" ]]; then
	SC_ENV+=(VKSC_EMU_VK_LAYER_PATH="${QVKSC_VALIDATION_DIR}"
		VKSC_EMU_VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
		VK_LOADER_DEBUG=layer)
fi

# run_game <name> <renderer> [env assignments...] -- [game arguments...]
run_game()
{
	local name="$1" renderer="$2"
	shift 2
	local env_vars=()
	while [[ $# -gt 0 && "$1" != "--" ]]; do
		env_vars+=("$1")
		shift
	done
	shift
	mkdir -p "${WORK_DIR}/${name}/baseq3"
	cp "${SRC_DIR}/scripts/vulkansc_scenes.cfg" "${WORK_DIR}/${name}/baseq3/"
	# VK_DRIVER_FILES names the Vulkan SC ICD. The desktop Vulkan loader must
	# not load it: the emulation ICD calls the desktop loader while the loader
	# scans the drivers, and the two wait for each other.
	local unset_vars=()
	if [[ "${renderer}" != "vulkansc" ]]; then
		unset_vars=(-u VK_DRIVER_FILES -u VK_LOADER_DEBUG)
	fi
	env "${unset_vars[@]}" "${env_vars[@]}" timeout 300 "${SRC_DIR}/run_vkquake3.sh" -r "${renderer}" -t "${BUILD_TYPE}" \
		-d "${DATA_DIR}" -u "${WORK_DIR}/${name}" -- "${COMMON_ARGS[@]}" "$@" \
		> "${WORK_DIR}/${name}.log" 2>&1
	echo $? > "${WORK_DIR}/${name}.exit"
}

# check_log <name> <description> [<validation>]: prints the result line, and
# counts a failure. <validation> is 1 when the run must have the validation
# layer active; the default is 1 with QVKSC_VALIDATION_DIR.
check_log()
{
	local name="$1" description="$2" validation="${3-${QVKSC_VALIDATION_DIR:+1}}"
	if python3 -I - "${WORK_DIR}/${name}.log" "$(cat "${WORK_DIR}/${name}.exit")" "${description}" \
		"${validation}" <<'PYEOF'
import re
import sys

log_path, exit_code, description, validation = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
lines = open(log_path, errors="replace").read().splitlines()

problems = []
if exit_code != 0:
    problems.append(f"exit code {exit_code}")
checks = (
    ("validation messages", r"VUID-|Validation Error|\[VALIDATION\]"),
    ("Vulkan errors", r"^Vulkan: error|^Vulkan SC: could not"),
    ("pipelines missing from the cache", r"is not in the pipeline manifest"),
    ("full image chunks", r"image chunks are full"),
    ("errors", r"^\^?[0-9]?\s*(ERROR|Error):"),
)
for label, pattern in checks:
    found = [l for l in lines if re.search(pattern, l)]
    if found:
        problems.append(f"{len(found)} {label} (first: {found[0][:160]})")
allocations = [l for l in lines if re.search(r"device memory allocations, \d+ after startup", l)]
if not allocations:
    problems.append("no device memory report (the renderer did not shut down?)")
elif not re.search(r", 0 after startup", allocations[-1]):
    problems.append(allocations[-1].strip())
if validation and not any('Insert instance layer "VK_LAYER_KHRONOS_validation"' in l for l in lines):
    problems.append("the validation layer is not active")

status = "PASS" if not problems else "FAIL"
print(f"{status}  {description}")
for problem in problems:
    print(f"        {problem}")
sys.exit(0 if not problems else 1)
PYEOF
	then
		return 0
	fi
	FAILURES=$((FAILURES + 1))
	return 1
}

# to_png <dir>: converts the screenshots of a run (TGA) to PNG files in <dir>.
to_png()
{
	local run_dir="$1" out_dir="$2"
	mkdir -p "${out_dir}"
	python3 -I - "${run_dir}/baseq3/screenshots" "${out_dir}" <<'PYEOF'
import pathlib
import sys
from PIL import Image

for tga in sorted(pathlib.Path(sys.argv[1]).glob("*.tga")):
    with Image.open(tga) as image:
        image.convert("RGB").save(pathlib.Path(sys.argv[2]) / (tga.stem + ".png"))
PYEOF
}

# compare <description> <ref dir> <test dir>
compare()
{
	local description="$1" ref_dir="$2" test_dir="$3"
	if [[ -z "$(ls "${ref_dir}" 2>/dev/null)" || -z "$(ls "${test_dir}" 2>/dev/null)" ]]; then
		echo "FAIL  ${description}"
		echo "        no screenshots"
		FAILURES=$((FAILURES + 1))
		return
	fi
	if python3 -I "${SRC_DIR}/scripts/compare_images.py" -t "${COMPARE_THRESHOLD}" -l "${COMPARE_LIMIT_PERCENT}" \
		"${ref_dir}" "${test_dir}" > "${WORK_DIR}/compare.log" 2>&1; then
		echo "PASS  ${description}"
	else
		echo "FAIL  ${description}"
		FAILURES=$((FAILURES + 1))
	fi
	sed 's/^/        /' "${WORK_DIR}/compare.log"
}

echo "Work directory: ${WORK_DIR}"
DESKTOP_SIZE=(+set r_mode -1 +set r_customwidth 960 +set r_customheight 540 +set r_fullscreen 0)

run_game demo vulkansc "${SC_ENV[@]}" -- +demo four +wait 300 +quit
check_log demo "demo: 300 frames of demo four"

run_game memory vulkansc "${SC_ENV[@]}" -- +set r_picmip 0 +demo four +wait 200 +devmap q3dm15 +wait 20 +quit
check_log memory "memory: demo four, then q3dm15, with r_picmip 0"
grep -h "image chunks: peak" "${WORK_DIR}/memory.log" | tail -1 | sed 's/^/        /'

# The display mode of the emulation is 960x540. The desktop run uses the same size.
run_game scenes_sc vulkansc "${SC_ENV[@]}" -- +set fixedtime 16 +exec vulkansc_scenes.cfg
check_log scenes_sc "scenes: menu, mirror, portal, fog"
run_game scenes_desktop vulkan -- "${DESKTOP_SIZE[@]}" +set fixedtime 16 +exec vulkansc_scenes.cfg
to_png "${WORK_DIR}/scenes_sc" "${WORK_DIR}/png/scenes_sc"
to_png "${WORK_DIR}/scenes_desktop" "${WORK_DIR}/png/scenes_desktop"
compare "scenes: Vulkan SC against desktop Vulkan" "${WORK_DIR}/png/scenes_desktop" "${WORK_DIR}/png/scenes_sc"

FRAME_ARGS=(+set timedemo 1 +set cg_drawFPS 0 +demo four +wait 200 +screenshot frame_0200 +wait 30 +quit)
run_game frame_sc vulkansc "${SC_ENV[@]}" -- "${FRAME_ARGS[@]}"
check_log frame_sc "frame: frame 200 of demo four"
run_game frame_desktop vulkan -- "${DESKTOP_SIZE[@]}" "${FRAME_ARGS[@]}"
to_png "${WORK_DIR}/frame_sc" "${WORK_DIR}/png/frame_sc"
to_png "${WORK_DIR}/frame_desktop" "${WORK_DIR}/png/frame_desktop"
compare "frame: Vulkan SC against desktop Vulkan" "${WORK_DIR}/png/frame_desktop" "${WORK_DIR}/png/frame_sc"

if [[ -n "${GFXR_VULKANSC_BUILD:-}" ]]; then
	LAYER="${GFXR_VULKANSC_BUILD}/layer/vulkansc/libvulkansc_gfxrecon.so"
	REPLAY="${GFXR_VULKANSC_BUILD}/tools/premium_replay/gfxrecon-premium-replay"
	INFO="${GFXR_VULKANSC_BUILD}/tools/premium_info/gfxrecon-premium-info"
	CAPTURE="${WORK_DIR}/capture.gfxr"
	if [[ -z "${QVKSC_LIBRARY:-}" ]]; then
		echo "SKIP  capture: set QVKSC_LIBRARY (the capture layer forwards to the loader)"
	else
		# The layer takes the place of the loader, and forwards to it.
		# The page guard memory tracking of the layer uses SIGSEGV. The game sets
		# its SIGSEGV handler after Com_Init, that is, after the layer set its own,
		# so the layer must watch for that and set its handler again.
		run_game capture vulkansc QVKSC_PIPELINE_DIR="${PIPELINE_DIR}" QVKSC_LIBRARY="${LAYER}" \
			GFXRECON_VULKANSC_DRIVER_PATH="${QVKSC_LIBRARY}" GFXRECON_CAPTURE_FILE="${CAPTURE}" \
			GFXRECON_CAPTURE_FILE_TIMESTAMP=false GFXRECON_PAGE_GUARD_SIGNAL_HANDLER_WATCHER=true \
			-- +demo four +wait 300 +quit
		check_log capture "capture: demo four with the Premium-GFXR Vulkan SC layer" ""
		GFXRECON_VULKANSC_DRIVER_PATH="${QVKSC_LIBRARY}" timeout 600 "${REPLAY}" "${CAPTURE}" \
			> "${WORK_DIR}/replay.log" 2>&1
		replay_exit=$?
		"${INFO}" "${CAPTURE}" > "${WORK_DIR}/capture_info.log" 2>&1
		frames="$(sed -n 's/.*Measured FPS: .* \([0-9]*\) frames,.*/\1/p' "${WORK_DIR}/replay.log" | tail -1)"
		faults="$(sed -n 's/^[[:space:]]*Faults:[[:space:]]*\([0-9]*\).*/\1/p' "${WORK_DIR}/capture_info.log" | head -1)"
		errors="$(sed -n '/Debug Messages:/,/Faults:/s/^[[:space:]]*Error:[[:space:]]*\([0-9]*\).*/\1/p' "${WORK_DIR}/capture_info.log" | head -1)"
		sources="$(sed -n 's/^[[:space:]]*Pipeline Cache Sources:[[:space:]]*\([0-9]*\).*/\1/p' "${WORK_DIR}/capture_info.log" | head -1)"
		pipelines="$(wc -l < "${PIPELINE_DIR}/q3_pipelines.txt")"
		if [[ ${replay_exit} -eq 0 && -n "${frames}" && "${faults:-0}" == "0" && "${errors:-0}" == "0" \
			&& "${sources}" == "${pipelines}" ]]; then
			echo "PASS  capture: replayed ${frames} frames; no faults or error messages; ${sources} pipeline cache sources"
		else
			echo "FAIL  capture: replay exit ${replay_exit}, frames '${frames}', faults '${faults}', error messages '${errors}', pipeline cache sources '${sources}' of ${pipelines}"
			FAILURES=$((FAILURES + 1))
		fi
	fi
fi

if [[ ${KEEP} -eq 1 ]]; then
	echo "Logs and screenshots: ${WORK_DIR}"
fi
if [[ ${FAILURES} -gt 0 ]]; then
	echo "${FAILURES} test(s) failed"
	exit 1
fi
echo "All tests passed"
