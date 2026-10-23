#!/bin/bash

# Compile the GLSL shaders to SPIR-V, and write the SPIR-V as C arrays for
# the Vulkan backend. Run this script in its own directory.
OUT=../../Vulkan/shaders

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
