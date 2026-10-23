#!/usr/bin/env bash
# Check that the checked-in shaders are the output of the GLSL shaders.
#
# The script runs graphics_api/shaders/glsl/compile.sh into a temporary
# directory and compares each output file with the checked-in file in
# graphics_api/Vulkan/shaders and graphics_api/WebGPU/shaders. It exits with
# status 1 if a file is different or missing.
#
# The SPIR-V and the WGSL depend on the versions of glslangValidator,
# spirv-opt, and tint. A different version can give a different file from the
# same GLSL. If the check fails after a tool update, run compile.sh and look at
# the differences before you commit them.
#
# Usage: check_shaders.sh
#
# Set TINT to use a tint executable that is not in build/dawn-install/bin.
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAPI_DIR="${SRC_DIR}/code/renderer_gapi/graphics_api"
GLSL_DIR="${GAPI_DIR}/shaders/glsl"

for tool in glslangValidator spirv-opt gcc; do
	if ! command -v "${tool}" > /dev/null; then
		echo "${tool} is necessary to check the shaders." >&2
		exit 1
	fi
done

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT
mkdir -p "${TMP}/vulkan" "${TMP}/webgpu"

# compile.sh runs in its own directory and builds bintoc there if it is
# missing. Keep the source tree clean: remove bintoc if this script made it.
remove_bintoc=0
if [[ ! -e "${GLSL_DIR}/bintoc" ]]; then
	remove_bintoc=1
fi
export TINT="${TINT:-${SRC_DIR}/build/dawn-install/bin/tint}"
status=0
(cd "${GLSL_DIR}" && OUT="${TMP}/vulkan" WGSL_OUT="${TMP}/webgpu" ./compile.sh > "${TMP}/compile.log" 2>&1) \
	|| status=$?
if (( remove_bintoc )); then
	rm -f "${GLSL_DIR}/bintoc"
fi
if (( status != 0 )); then
	cat "${TMP}/compile.log" >&2
	echo "compile.sh failed." >&2
	exit 1
fi

echo "glslangValidator $(glslangValidator --version | sed -n '1s/.*: *//p'), $(spirv-opt --version 2>&1 | head -n 1)"

failed=0
checked=0
compare_dir()
{
	local generated="$1" checked_in="$2"
	local file name
	for file in "${generated}"/*; do
		name="$(basename "${file}")"
		checked=$((checked + 1))
		if [[ ! -e "${checked_in}/${name}" ]]; then
			echo "MISSING ${checked_in#"${SRC_DIR}"/}/${name}"
			failed=1
		elif ! cmp -s "${file}" "${checked_in}/${name}"; then
			echo "DIFFERENT ${checked_in#"${SRC_DIR}"/}/${name}"
			failed=1
		fi
	done
}
compare_dir "${TMP}/vulkan" "${GAPI_DIR}/Vulkan/shaders"
compare_dir "${TMP}/webgpu" "${GAPI_DIR}/WebGPU/shaders"

if (( failed )); then
	echo "FAILED: the checked-in shaders are not the output of compile.sh."
	exit 1
fi
echo "OK: ${checked} shader files are equal to the output of compile.sh."
