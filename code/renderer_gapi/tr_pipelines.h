#ifndef TR_PIPELINES_H_
#define TR_PIPELINES_H_

#include "tr_local.h"

void create_standard_pipelines(void);
void create_pipelines_for_each_stage(shaderStage_t *pStage, shader_t* pShader);

void R_DestroyShaderStagePipelines(void);
void R_DestroyStandardPipelines(void);

void R_PipelineList_f(void);

struct GlobalPipelineManager {
	//
	// Standard pipelines.
	//
	gapi_pipeline_t* skybox_pipeline;

	// dim 0: 0 - front side, 1 - back size
	// dim 1: 0 - normal view, 1 - mirror view
	gapi_pipeline_t* shadow_volume_pipelines[2][2];
	gapi_pipeline_t* shadow_finish_pipeline;

	// dim 0 is based on fogPass_t: 0 - corresponds to FP_EQUAL, 1 - corresponds to FP_LE.
	// dim 1 is directly a cullType_t enum value.
	// dim 2 is a polygon offset value (0 - off, 1 - on).
	gapi_pipeline_t* fog_pipelines[2][3][2];

	// dim 0 is based on dlight additive flag: 0 - not additive, 1 - additive
	// dim 1 is directly a cullType_t enum value.
	// dim 2 is a polygon offset value (0 - off, 1 - on).
	gapi_pipeline_t* dlight_pipelines[2][3][2];

	// debug visualization pipelines
	gapi_pipeline_t* tris_debug_pipeline;
	gapi_pipeline_t* tris_mirror_debug_pipeline;
	gapi_pipeline_t* tris_lines_debug_pipeline;		// without fillModeLine: RB_DrawTris gives lines
	gapi_pipeline_t* normals_debug_pipeline;
	gapi_pipeline_t* surface_debug_pipeline_solid;
	gapi_pipeline_t* surface_debug_pipeline_outline;
	gapi_pipeline_t* images_debug_pipeline;
};

extern struct GlobalPipelineManager g_stdPipelines;


#endif
