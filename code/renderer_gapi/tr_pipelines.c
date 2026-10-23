#include "tr_local.h"
#include "tr_pipelines.h"
#include "tr_state_bits.h"
#include "tr_shader.h"
#include "ref_import.h"

// The cache of graphics pipelines. Each Q3 shader stage gets three pipelines
// (normal, portal, and mirror view), and stages with the same state share
// them. The graphics API creates the pipelines (gapi_createPipeline).


struct GlobalPipelineManager g_stdPipelines;

#define MAX_STAGE_PIPELINES     1024

typedef struct {
    gapi_pipelineDesc_t desc;
    gapi_pipeline_t* pipeline;
} stagePipeline_t;

static stagePipeline_t s_stagePipelines[MAX_STAGE_PIPELINES];
static uint32_t s_numPipelines = 0;

// The standard pipelines in g_stdPipelines.
#define MAX_STD_PIPELINES       64
static gapi_pipeline_t* s_stdPipelines[MAX_STD_PIPELINES];
static uint32_t s_numStdPipelines = 0;


void R_PipelineList_f(void)
{
    ri.Printf(PRINT_ALL, " Total pipeline created: %d\n", s_numPipelines);
}


static gapi_cullType_t R_GapiCullType(cullType_t cullType)
{
    switch (cullType)
    {
        case CT_FRONT_SIDED: return GAPI_CULL_FRONT_SIDED;
        case CT_BACK_SIDED:  return GAPI_CULL_BACK_SIDED;
        case CT_TWO_SIDED:   return GAPI_CULL_TWO_SIDED;
    }
    return GAPI_CULL_FRONT_SIDED;
}


static gapi_pipeline_t* R_FindPipeline(const gapi_pipelineDesc_t* def)
{
    uint32_t i = 0;
	for (i = 0; i < s_numPipelines; i++)
    {
        const gapi_pipelineDesc_t* cached = &s_stagePipelines[i].desc;
		if (cached->shaderType == def->shaderType &&
			cached->stateBits == def->stateBits &&
			cached->cullType == def->cullType &&
			cached->polygonOffset == def->polygonOffset &&
			cached->clipPlane == def->clipPlane &&
			cached->mirror == def->mirror 
			// && cached->linePrimitives == def->linePrimitives
			// && cached->shadowPhase == def->shadowPhase
            )
		{
			return s_stagePipelines[i].pipeline;
		}
	}

	gapi_pipeline_t* pipeline = gapi_createPipeline(def);
	s_stagePipelines[s_numPipelines].desc = *def;
	s_stagePipelines[s_numPipelines].pipeline = pipeline;

    if (++s_numPipelines >= MAX_STAGE_PIPELINES)
    {
		ri.Error(ERR_DROP, "R_FindPipeline: MAX_STAGE_PIPELINES hit\n");
	}
	return pipeline;
}


static void R_CreateStdPipeline(const gapi_pipelineDesc_t* def, gapi_pipeline_t** pHandle)
{
    if (s_numStdPipelines >= MAX_STD_PIPELINES)
    {
        ri.Error(ERR_FATAL, "R_CreateStdPipeline: MAX_STD_PIPELINES hit\n");
    }

    *pHandle = gapi_createPipeline(def);
    s_stdPipelines[s_numStdPipelines++] = *pHandle;
}


void create_pipelines_for_each_stage(shaderStage_t *pStage, shader_t* pShader)
{
    gapi_pipelineDesc_t def;

    def.linePrimitives = 0;
    def.shadowPhase = 0;
    def.cullType = R_GapiCullType(pShader->cullType);
    def.polygonOffset = pShader->polygonOffset ? qtrue : qfalse;
    def.stateBits = pStage->stateBits;

    if (pStage->bundle[1].image[0] == NULL)
        def.shaderType = GAPI_SHADER_SINGLE_TEXTURE;
    else if (pShader->multitextureEnv == GL_MODULATE)
        def.shaderType = GAPI_SHADER_MULTI_TEXTURE_MUL;
    else if (pShader->multitextureEnv == GL_ADD)
        def.shaderType = GAPI_SHADER_MULTI_TEXTURE_ADD;
    else
        ri.Error(ERR_FATAL, "could not create pipelines for q3 shader '%s'\n", pShader->name);

    def.clipPlane = qfalse;
    def.mirror = qfalse;
    pStage->pipeline = R_FindPipeline(&def);


    def.clipPlane = qtrue;
    def.mirror = qfalse;
    pStage->portalPipeline = R_FindPipeline(&def);


    def.clipPlane = qtrue;
    def.mirror = qtrue;
    pStage->mirrorPipeline = R_FindPipeline(&def);
}


void create_standard_pipelines(void)
{
      
    ri.Printf(PRINT_ALL, " Create skybox pipeline \n");
    {

        gapi_pipelineDesc_t def;
        memset(&def, 0, sizeof(def));

        def.shaderType = GAPI_SHADER_SINGLE_TEXTURE;
        def.stateBits = 0;
        def.cullType = GAPI_CULL_FRONT_SIDED;
        def.polygonOffset = qfalse;
        def.clipPlane = qfalse;
        def.mirror = qfalse;
        
        R_CreateStdPipeline(&def, &g_stdPipelines.skybox_pipeline);
    }

    ri.Printf(PRINT_ALL, " Create Q3 stencil shadows pipeline \n");
    {
        {
            gapi_pipelineDesc_t def;
            memset(&def, 0, sizeof(def));


            def.polygonOffset = qfalse;
            def.stateBits = 0;
            def.shaderType = GAPI_SHADER_SINGLE_TEXTURE;
            def.clipPlane = qfalse;
            def.shadowPhase = GAPI_SHADOW_EDGES;

            gapi_cullType_t cull_types[2] = {GAPI_CULL_FRONT_SIDED, GAPI_CULL_BACK_SIDED};
            qboolean mirror_flags[2] = {qfalse, qtrue};

            int i = 0; 
            int j = 0;

            for (i = 0; i < 2; i++)
            {
                def.cullType = cull_types[i];
                for (j = 0; j < 2; j++)
                {
                    def.mirror = mirror_flags[j];
                    
                    R_CreateStdPipeline(&def, &g_stdPipelines.shadow_volume_pipelines[i][j]);
                }
            }
        }

        {
            gapi_pipelineDesc_t def;
            memset(&def, 0, sizeof(def));

            def.cullType = GAPI_CULL_FRONT_SIDED;
            def.polygonOffset = qfalse;
            def.stateBits = GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO;
            def.shaderType = GAPI_SHADER_SINGLE_TEXTURE;
            def.clipPlane = qfalse;
            def.mirror = qfalse;
            def.shadowPhase = GAPI_SHADOW_FULLSCREEN_QUAD;
            
            R_CreateStdPipeline(&def, &g_stdPipelines.shadow_finish_pipeline);
        }
    }


    ri.Printf(PRINT_ALL, " Create fog and dlights pipeline \n");
    {
        gapi_pipelineDesc_t def;
        memset(&def, 0, sizeof(def));


        def.shaderType = GAPI_SHADER_SINGLE_TEXTURE;
        def.clipPlane = qfalse;
        def.mirror = qfalse;

        unsigned int fog_state_bits[2] = {
            GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL,
            GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA
        };
        unsigned int dlight_state_bits[2] = {
            GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL,
            GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL
        };
        
        qboolean polygon_offset[2] = {qfalse, qtrue};

        int i = 0, j = 0, k = 0;

        for (i = 0; i < 2; i++)
        {
            unsigned fog_state = fog_state_bits[i];
            unsigned dlight_state = dlight_state_bits[i];

            for (j = 0; j < 3; j++)
            {
                def.cullType = (gapi_cullType_t)j; // cullType_t value

                for ( k = 0; k < 2; k++)
                {
                    def.polygonOffset = polygon_offset[k];

                    def.stateBits = fog_state;
                    R_CreateStdPipeline(&def, &g_stdPipelines.fog_pipelines[i][j][k]);

                    def.stateBits = dlight_state;
                    R_CreateStdPipeline(&def, &g_stdPipelines.dlight_pipelines[i][j][k]);
                }
            }
        }
    }


    // debug pipelines
    ri.Printf(PRINT_ALL, " Create tris debug pipeline \n");
    {
        gapi_pipelineDesc_t def;
        memset(&def, 0, sizeof(def));

        def.stateBits = GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE;
        R_CreateStdPipeline(&def, &g_stdPipelines.tris_debug_pipeline);
    }

    
    ri.Printf(PRINT_ALL, " Create tris mirror debug pipeline \n");
    {
        gapi_pipelineDesc_t def;
        memset(&def, 0, sizeof(def));

        def.stateBits = GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE;
        def.cullType = GAPI_CULL_BACK_SIDED;
        R_CreateStdPipeline(&def, &g_stdPipelines.tris_mirror_debug_pipeline);
    }

    ri.Printf(PRINT_ALL, " Create normals debug pipeline \n");
    {
        gapi_pipelineDesc_t def;
        memset(&def, 0, sizeof(def));

        def.stateBits = GLS_DEPTHMASK_TRUE;
        def.linePrimitives = qtrue;
        R_CreateStdPipeline(&def, &g_stdPipelines.normals_debug_pipeline);
    }


    ri.Printf(PRINT_ALL, " Create surface debug pipeline \n");
    {
        gapi_pipelineDesc_t def;
        memset(&def, 0, sizeof(def));

        def.stateBits = GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
        R_CreateStdPipeline(&def, &g_stdPipelines.surface_debug_pipeline_solid);
    }

    ri.Printf(PRINT_ALL, " Create surface debug outline pipeline \n");
    {
        gapi_pipelineDesc_t def;
        memset(&def, 0, sizeof(def));

        def.stateBits = GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
        def.linePrimitives = qtrue;
        R_CreateStdPipeline(&def, &g_stdPipelines.surface_debug_pipeline_outline);
    }
    
    ri.Printf(PRINT_ALL, " Create images debug pipeline \n");
    {
        gapi_pipelineDesc_t def;
        memset(&def, 0, sizeof(def));

        def.stateBits = GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
        R_CreateStdPipeline(&def, &g_stdPipelines.images_debug_pipeline);
    }
}


void R_DestroyShaderStagePipelines(void)
{
    // shader stage
    gapi_waitIdle();
    uint32_t i;
    for (i = 0; i < s_numPipelines; i++)
    {
		gapi_destroyPipeline(s_stagePipelines[i].pipeline);
        memset(&s_stagePipelines[i], 0, sizeof(s_stagePipelines[i]));
    }
    s_numPipelines = 0;
}


void R_DestroyStandardPipelines(void)
{
    uint32_t i;
    for (i = 0; i < s_numStdPipelines; i++)
    {
        gapi_destroyPipeline(s_stdPipelines[i]);
    }
    memset(s_stdPipelines, 0, sizeof(s_stdPipelines));
    s_numStdPipelines = 0;
    memset(&g_stdPipelines, 0, sizeof(g_stdPipelines));
}
