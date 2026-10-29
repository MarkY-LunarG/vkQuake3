#!/usr/bin/env bash
# Capture reference images and a timedemo result with one renderer, for image
# comparison between renderers or between two versions of a renderer.
#
# The script plays "demo four" (demos/four.dm_68 in pak8.pk3) and records it
# with the "video" command. The client then uses a fixed frame time, so two
# runs give the same frames. The script keeps one frame in each <keep> as a
# PNG file, then deletes the video.
#
# Then it runs the fixed scenes in scripts/gapi_scenes.cfg (mirror, portal,
# shadows, debug lines, fog, menu) with "fixedtime 16", and saves each
# screenshot as scene_<number>_<name>.png.
#
# Last, it runs "timedemo 1" with the same demo and saves the result line and
# the CPU time of the run in timedemo.txt.
#
# By default, the game runs on a new Xvfb display with the lavapipe Vulkan
# driver (llvmpipe, on the CPU). Then the desktop mouse and keyboard cannot
# change the capture. With -w, the game opens a window on the desktop and uses
# the GPU. Do not use that window during the capture: a key press or a mouse
# click stops a demo or fires the weapon. The two modes give slightly
# different images, so compare captures of the same mode only.
#
# Usage: capture_vkquake3.sh [-r <renderer>] [-t <type>] [-o <dir>] [-m <mode>]
#                            [-f <fps>] [-k <keep>] [-s] [-n] [-w] [-- <game args>]
#
#   -r <renderer> Renderer: vulkan (default), opengl1, or opengl2.
#   -t <type>     Build type: release (default) or debug.
#   -o <dir>      Output directory. The default is "build_reference/<renderer>_xvfb",
#                 or "build_reference/<renderer>" with -w.
#                 The script deletes the PNG files in it first.
#   -m <mode>     r_mode value. The default is 6 (1024x768).
#   -f <fps>      Video frame rate (cl_aviFrameRate). The default is 10.
#   -k <keep>     Keep one frame in each <keep> frames. The default is 10.
#   -s            Do not run the fixed scenes.
#   -n            Do not run the timedemo.
#   -w            Run in a window on the desktop with the GPU, not on Xvfb.
#   -h            Show this help.
#
# The game data comes from run_vkquake3.sh (-d and $Q3_DATA work the same).
# Each run uses a new user directory, "build_userdata/capture", so that the
# cvars always have their default values.
#
# Arguments after "--" go to the game without change.
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RENDERER="vulkan"
BUILD_TYPE="release"
OUT_DIR=""
MODE=6
FPS=10
KEEP=10
SCENES=1
TIMEDEMO=1
DESKTOP=0

usage()
{
	sed -n '2,43s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

while getopts ":r:t:o:m:f:k:snwh" opt; do
	case "${opt}" in
		r) RENDERER="${OPTARG}" ;;
		t) BUILD_TYPE="${OPTARG}" ;;
		o) OUT_DIR="${OPTARG}" ;;
		m) MODE="${OPTARG}" ;;
		f) FPS="${OPTARG}" ;;
		k) KEEP="${OPTARG}" ;;
		s) SCENES=0 ;;
		n) TIMEDEMO=0 ;;
		w) DESKTOP=1 ;;
		h) usage; exit 0 ;;
		:) echo "Option -${OPTARG} needs a value." >&2; usage >&2; exit 2 ;;
		*) echo "Unknown option: -${OPTARG}" >&2; usage >&2; exit 2 ;;
	esac
done
shift $((OPTIND - 1))
if [[ "${1:-}" == "--" ]]; then
	shift
fi

TOOLS=(ffmpeg magick)
if (( ! DESKTOP )); then
	TOOLS+=(xvfb-run)
fi
for tool in "${TOOLS[@]}"; do
	if ! command -v "${tool}" > /dev/null; then
		echo "${tool} is necessary for the capture." >&2
		exit 1
	fi
done

if [[ -z "${OUT_DIR}" ]]; then
	if (( DESKTOP )); then
		OUT_DIR="${SRC_DIR}/build_reference/${RENDERER}"
	else
		OUT_DIR="${SRC_DIR}/build_reference/${RENDERER}_xvfb"
	fi
fi
USER_DIR="${SRC_DIR}/build_userdata/capture"
VIDEO="${USER_DIR}/baseq3/videos/capture.avi"

COMMON_ARGS=(
	+set s_initsound 0
	+set r_fullscreen 0
	+set r_mode "${MODE}"
	+set com_introplayed 1
	# The lagometer uses real time, so it changes between runs.
	+set cg_lagometer 0
	# With -w, mouse motion over the window turns the view.
	+set in_mouse 0
	+set nextdemo quit
)

LAVAPIPE_ICD="/usr/share/vulkan/icd.d/lvp_icd.$(uname -m).json"

# Run the game with run_vkquake3.sh, on Xvfb unless -w is set.
run_game()
{
	if (( DESKTOP )); then
		"${SRC_DIR}/run_vkquake3.sh" "$@"
	else
		env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 VK_DRIVER_FILES="${LAVAPIPE_ICD}" \
			xvfb-run -a -s "-screen 0 1280x1024x24 -nolisten tcp" \
			"${SRC_DIR}/run_vkquake3.sh" "$@"
	fi
}

if (( ! DESKTOP )) && [[ ! -f "${LAVAPIPE_ICD}" ]]; then
	echo "The lavapipe Vulkan driver is not installed: ${LAVAPIPE_ICD}" >&2
	echo "Install mesa-vulkan-drivers, or use -w." >&2
	exit 1
fi

rm -rf "${USER_DIR}"
mkdir -p "${OUT_DIR}"
rm -f "${OUT_DIR}"/frame_*.png "${OUT_DIR}"/scene_*.png

echo "Recording demo four with the ${RENDERER} renderer"
run_game -r "${RENDERER}" -t "${BUILD_TYPE}" -u "${USER_DIR}" -- \
	"${COMMON_ARGS[@]}" \
	+set cl_aviFrameRate "${FPS}" \
	+set cl_aviMotionJpeg 0 \
	+demo four +video capture \
	"$@" > "${OUT_DIR}/capture.log" 2>&1

if [[ ! -f "${VIDEO}" ]]; then
	echo "The game did not write ${VIDEO}. See ${OUT_DIR}/capture.log." >&2
	exit 1
fi
# demo four is 63 seconds long. With -w, a key press or a mouse click in the
# game window stops a demo, and the video is then shorter.
written="$(sed -n 's/^Wrote \([0-9]*\):.* frames to videos\/capture.avi.*/\1/p' "${OUT_DIR}/capture.log")"
if (( ${written:-0} < 60 * FPS )); then
	echo "The demo stopped early (${written:-0} video frames). A key press or a mouse" >&2
	echo "click in the game window stops a demo. Run the capture again." >&2
	rm -f "${VIDEO}"
	exit 1
fi

ffmpeg -loglevel error -i "${VIDEO}" \
	-vf "select=not(mod(n\\,${KEEP}))" -fps_mode passthrough \
	"${OUT_DIR}/frame_%04d.png"
rm -f "${VIDEO}"
echo "$(find "${OUT_DIR}" -name 'frame_*.png' | wc -l) frames in ${OUT_DIR}"

if (( SCENES )); then
	echo "Running the fixed scenes"
	rm -rf "${USER_DIR}"
	mkdir -p "${USER_DIR}/baseq3"
	cp "${SRC_DIR}/scripts/gapi_scenes.cfg" "${USER_DIR}/baseq3/"
	# fixedtime is a cheat cvar. It is accepted here because no server runs
	# yet, and devmap keeps cheats on.
	run_game -r "${RENDERER}" -t "${BUILD_TYPE}" -u "${USER_DIR}" -- \
		"${COMMON_ARGS[@]}" \
		+set fixedtime 16 \
		+exec gapi_scenes.cfg \
		"$@" > "${OUT_DIR}/scenes.log" 2>&1
	for tga in "${USER_DIR}"/baseq3/screenshots/scene_*.tga; do
		if [[ ! -f "${tga}" ]]; then
			echo "The game did not write the scene screenshots. See ${OUT_DIR}/scenes.log." >&2
			exit 1
		fi
		name="$(basename "${tga}" .tga)"
		magick "${tga}" "${OUT_DIR}/${name}.png"
	done
	echo "$(find "${OUT_DIR}" -name 'scene_*.png' | wc -l) scenes in ${OUT_DIR}"
fi

if (( TIMEDEMO )); then
	echo "Running timedemo"
	rm -rf "${USER_DIR}"
	# The timedemo waits at least 1 ms for each frame (Com_Frame), so its
	# result cannot go above 1000 fps. The game sleeps during this wait, so
	# the CPU time of the process also shows the work of each frame.
	TIMEFORMAT="cpu_user=%U cpu_sys=%S wall=%R"
	{ time run_game -r "${RENDERER}" -t "${BUILD_TYPE}" -u "${USER_DIR}" -- \
		"${COMMON_ARGS[@]}" \
		+timedemo 1 +demo four \
		"$@" > "${OUT_DIR}/timedemo.log" 2>&1 ; } 2> "${OUT_DIR}/timedemo_time.txt"
	{
		grep -E '^[0-9]+ frames ' "${OUT_DIR}/timedemo.log"
		cat "${OUT_DIR}/timedemo_time.txt"
	} | tee "${OUT_DIR}/timedemo.txt"
	rm -f "${OUT_DIR}/timedemo_time.txt"
fi
