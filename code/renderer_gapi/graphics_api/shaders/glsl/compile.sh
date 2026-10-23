#!/bin/bash

# Compile the GLSL shaders for the graphics APIs, and write them as C arrays.
# Run this script in its own directory.
#
#   Vulkan: GLSL to SPIR-V with glslangValidator.
#   WebGPU: GLSL with GAPI_WEBGPU to SPIR-V, split the combined image samplers
#           with spirv-opt, then SPIR-V to WGSL with tint. The tint executable
#           comes from scripts/build_dawn.sh. Set TINT to use a different one.
#
# Set OUT and WGSL_OUT to write the Vulkan and the WebGPU files to other
# directories. scripts/check_shaders.sh uses this.
OUT=${OUT:-../../Vulkan/shaders}
WGSL_OUT=${WGSL_OUT:-../../WebGPU/shaders}
TINT=${TINT:-../../../../../build/dawn-install/bin/tint}

if [[ ! -x "./bintoc" ]]
then
	gcc bintoc.c -o bintoc
fi

find -type f -name "*.vert" | \
	while read f; do glslangValidator -V ${f} -o "${OUT}/${f%.*}.vspv"; done

find -type f -name "*.frag" | \
	while read f; do glslangValidator -V ${f} -o "${OUT}/${f%.*}.fspv"; done



find ${OUT} -type f -name "*.vspv" | \
	while read f; do ./bintoc ${f} `basename ${f%.*}`_vert_spv > ${f%.*}_vert.c; done

find ${OUT} -type f -name "*.fspv" | \
	while read f; do ./bintoc ${f} `basename ${f%.*}`_frag_spv > ${f%.*}_frag.c; done


if [[ ! -x "${TINT}" ]]
then
	echo "tint is not at ${TINT}. Run scripts/build_dawn.sh, or set TINT." >&2
	exit 1
fi

mkdir -p ${WGSL_OUT}
TMP=$(mktemp -d)
trap 'rm -rf "${TMP}"' EXIT

for f in *.vert *.frag
do
	name=${f%.*}
	stage=${f##*.}
	glslangValidator -V -DGAPI_WEBGPU ${f} -o "${TMP}/${name}.spv" > /dev/null || exit 1
	spirv-opt --split-combined-image-sampler "${TMP}/${name}.spv" -o "${TMP}/${name}.split.spv" || exit 1
	"${TINT}" --format wgsl "${TMP}/${name}.split.spv" -o "${WGSL_OUT}/${name}_${stage}.wgsl" || exit 1
	./bintoc "${WGSL_OUT}/${name}_${stage}.wgsl" ${name}_${stage}_wgsl > "${WGSL_OUT}/${name}_${stage}_wgsl.c"
done
