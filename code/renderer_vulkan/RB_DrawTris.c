#include "RB_DrawTris.h"
#include "tr_globals.h"
#include "vk_shade_geometry.h"
#include "vk_pipelines.h"
#include "tr_backend.h"
/*
================
Draws triangle outlines for debugging
================
*/
void RB_DrawTris (shaderCommands_t * pInput)
{
	if (vk.features.fillModeNonSolid == VK_FALSE) {
		static qboolean printed = qfalse;
		if (!printed) {
			ri.Printf(PRINT_WARNING, "RB_ShowTris: fillModeNonSolid not supported.\n");
			printed = qtrue;
		}
		return;
	}


	updateCurDescriptor( tr.whiteImage->tex, 0);

	// VULKAN

    memset(pInput->svars.colors, 255, pInput->numVertexes * 4 );
    const gapi_pipeline_t* pipeline = backEnd.viewParms.isMirror ? g_stdPipelines.tris_mirror_debug_pipeline : g_stdPipelines.tris_debug_pipeline;
    vk_shade_geometry(pipeline, qfalse, DEPTH_RANGE_ZERO, qtrue);
}
