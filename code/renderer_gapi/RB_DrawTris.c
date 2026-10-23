#include "RB_DrawTris.h"
#include "tr_globals.h"
#include "tr_shade_geometry.h"
#include "tr_pipelines.h"
#include "tr_backend.h"
#include "matrix_multiplication.h"


/*
================
Draws triangle outlines as lines, for a graphics API without fillModeLine.
The triangle pipelines cull the back faces, so this function does the same
on the CPU.
================
*/
static void RB_DrawTrisAsLines (shaderCommands_t * pInput)
{
	static vec3_t eyeXyz[SHADER_MAX_VERTEXES];
	static uint32_t lineIndexes[2 * SHADER_MAX_INDEXES];
	const float* modelView = getptr_modelview_matrix();
	// A mirror view changes the winding of the triangles.
	const float frontSign = backEnd.viewParms.isMirror ? -1.0f : 1.0f;
	uint32_t numLineIndexes = 0;
	int i;

	for (i = 0; i < pInput->numVertexes; i++)
		Vec3Transform(modelView, pInput->xyz[i], eyeXyz[i]);

	for (i = 0; i + 2 < pInput->numIndexes; i += 3)
	{
		const uint32_t i0 = pInput->indexes[i + 0];
		const uint32_t i1 = pInput->indexes[i + 1];
		const uint32_t i2 = pInput->indexes[i + 2];
		vec3_t edge1, edge2, normal;

		// The same front face test as R_DebugPolygon, in eye space. The 2D
		// triangles are in the plane of the eye, and the test cannot cull them.
		if (!backEnd.projection2D)
		{
			VectorSubtract(eyeXyz[i1], eyeXyz[i0], edge1);
			VectorSubtract(eyeXyz[i2], eyeXyz[i0], edge2);
			CrossProduct(edge2, edge1, normal);
			if (frontSign * DotProduct(normal, eyeXyz[i0]) >= 0)
				continue;
		}

		lineIndexes[numLineIndexes++] = i0;
		lineIndexes[numLineIndexes++] = i1;
		lineIndexes[numLineIndexes++] = i1;
		lineIndexes[numLineIndexes++] = i2;
		lineIndexes[numLineIndexes++] = i2;
		lineIndexes[numLineIndexes++] = i0;
	}

	if (numLineIndexes == 0)
		return;

	gapi_uploadPositionsAndIndexes(pInput->xyz, pInput->numVertexes, lineIndexes, numLineIndexes);

	gapi_draw_t draw;
	draw.colors = (const byte (*)[4]) pInput->svars.colors;
	draw.texCoords0 = (const vec2_t*) pInput->svars.texcoords[0];
	draw.texCoords1 = NULL;
	draw.numVertexes = pInput->numVertexes;
	draw.numIndexes = numLineIndexes;
	draw.depthRange = GAPI_DEPTH_ZERO;
	RB_GetDrawRect(&draw.viewport);
	draw.depthBiasConstant = 0;
	draw.depthBiasSlope = 0;
	gapi_draw(g_stdPipelines.tris_lines_debug_pipeline, &draw);
}


/*
================
Draws triangle outlines for debugging
================
*/
void RB_DrawTris (shaderCommands_t * pInput)
{
	gapi_caps_t caps;
	gapi_getCaps(&caps);

	gapi_bindTexture( 0, tr.whiteImage->tex );

	// VULKAN

    memset(pInput->svars.colors, 255, pInput->numVertexes * 4 );

	if (!caps.fillModeLine) {
		RB_DrawTrisAsLines(pInput);
		return;
	}

    const gapi_pipeline_t* pipeline = backEnd.viewParms.isMirror ? g_stdPipelines.tris_mirror_debug_pipeline : g_stdPipelines.tris_debug_pipeline;
    RB_ShadeGeometry(pipeline, qfalse, GAPI_DEPTH_ZERO, qtrue);
}
