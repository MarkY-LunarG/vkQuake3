#include "RB_DrawTris.h"
#include "tr_globals.h"
#include "tr_shade_geometry.h"
#include "tr_pipelines.h"
#include "tr_backend.h"
#include "ref_import.h"
/*
================
Draws triangle outlines for debugging
================
*/
void RB_DrawTris (shaderCommands_t * pInput)
{
	gapi_caps_t caps;
	gapi_getCaps(&caps);
	if (!caps.fillModeLine) {
		static qboolean printed = qfalse;
		if (!printed) {
			ri.Printf(PRINT_WARNING, "RB_ShowTris: fillModeNonSolid not supported.\n");
			printed = qtrue;
		}
		return;
	}


	gapi_bindTexture( 0, tr.whiteImage->tex );

	// VULKAN

    memset(pInput->svars.colors, 255, pInput->numVertexes * 4 );
    const gapi_pipeline_t* pipeline = backEnd.viewParms.isMirror ? g_stdPipelines.tris_mirror_debug_pipeline : g_stdPipelines.tris_debug_pipeline;
    RB_ShadeGeometry(pipeline, qfalse, GAPI_DEPTH_ZERO, qtrue);
}
