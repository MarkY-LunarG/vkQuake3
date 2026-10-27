// The WebGPU pipelines. The pipeline state comes from gapi_pipelineDesc_t
// with the same rules as the Vulkan backend (vk_pipeline.c). The shaders are
// the WGSL that compile.sh makes from the GLSL.

#include "wgpu_local.h"
#include "tr_state_bits.h"

// The WGSL shaders (graphics_api/WebGPU/shaders).
extern unsigned char single_texture_vert_wgsl[];
extern int single_texture_vert_wgsl_size;
extern unsigned char single_texture_clipping_plane_vert_wgsl[];
extern int single_texture_clipping_plane_vert_wgsl_size;
extern unsigned char single_texture_frag_wgsl[];
extern int single_texture_frag_wgsl_size;
extern unsigned char multi_texture_vert_wgsl[];
extern int multi_texture_vert_wgsl_size;
extern unsigned char multi_texture_clipping_plane_vert_wgsl[];
extern int multi_texture_clipping_plane_vert_wgsl_size;
extern unsigned char multi_texture_frag_wgsl[];
extern int multi_texture_frag_wgsl_size;

enum {
    SHADER_SINGLE_VERT,
    SHADER_SINGLE_CLIP_VERT,
    SHADER_SINGLE_FRAG,
    SHADER_MULTI_VERT,
    SHADER_MULTI_CLIP_VERT,
    SHADER_MULTI_FRAG,
    SHADER_COUNT
};

static WGPUShaderModule s_shaders[SHADER_COUNT];


// r_apiCoverage heavy: print the messages of the WGSL compiler.
static void wgpu_onCompilationInfo(WGPUCompilationInfoRequestStatus status, WGPUCompilationInfo const* info,
        void* userdata1, void* userdata2)
{
    if (status != WGPUCompilationInfoRequestStatus_Success || info == NULL)
        return;

    size_t i;
    for (i = 0; i < info->messageCount; i++)
    {
        const WGPUCompilationMessage* msg = &info->messages[i];
        char text[1024];
        ri.Printf(msg->type == WGPUCompilationMessageType_Info ? PRINT_DEVELOPER : PRINT_WARNING,
                "WebGPU: %s:%u:%u: %s\n", (const char*)userdata1, (unsigned)msg->lineNum, (unsigned)msg->linePos,
                wgpu_cString(msg->message, text, sizeof(text)));
    }
}


static WGPUShaderModule wgpu_createShader(const char* label, const unsigned char* code, int size)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code.data = (const char*)code;
    source.code.length = size;

    WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    desc.nextInChain = &source.chain;
    desc.label = wgpu_stringView(label);

    wgpu_pushErrorScope();
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(wg.device, &desc);
    wgpu_popErrorScope(label);

    if (wg.heavyCoverage)
    {
        WGPUCompilationInfoCallbackInfo callback = WGPU_COMPILATION_INFO_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_WaitAnyOnly;
        callback.callback = wgpu_onCompilationInfo;
        callback.userdata1 = (void*)label;
        WGPUFutureWaitInfo wait = WGPU_FUTURE_WAIT_INFO_INIT;
        wait.future = wgpuShaderModuleGetCompilationInfo(module, callback);
        wgpuInstanceWaitAny(wg.instance, 1, &wait, UINT64_MAX);
    }

    return module;
}


void wgpu_createPipelineResources(void)
{
    s_shaders[SHADER_SINGLE_VERT] = wgpu_createShader("single_texture.vert",
            single_texture_vert_wgsl, single_texture_vert_wgsl_size);
    s_shaders[SHADER_SINGLE_FRAG] = wgpu_createShader("single_texture.frag",
            single_texture_frag_wgsl, single_texture_frag_wgsl_size);
    s_shaders[SHADER_MULTI_VERT] = wgpu_createShader("multi_texture.vert",
            multi_texture_vert_wgsl, multi_texture_vert_wgsl_size);
    s_shaders[SHADER_MULTI_FRAG] = wgpu_createShader("multi_texture.frag",
            multi_texture_frag_wgsl, multi_texture_frag_wgsl_size);

    // The clip plane shaders need the clip-distances feature.
    if (wg.clipDistances)
    {
        s_shaders[SHADER_SINGLE_CLIP_VERT] = wgpu_createShader("single_texture_clipping_plane.vert",
                single_texture_clipping_plane_vert_wgsl, single_texture_clipping_plane_vert_wgsl_size);
        s_shaders[SHADER_MULTI_CLIP_VERT] = wgpu_createShader("multi_texture_clipping_plane.vert",
                multi_texture_clipping_plane_vert_wgsl, multi_texture_clipping_plane_vert_wgsl_size);
    }

    WGPUBindGroupLayout single[2] = { wg.uniformLayout, wg.textureLayout };
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = 2;
    desc.bindGroupLayouts = single;
    wg.layoutSingle = wgpuDeviceCreatePipelineLayout(wg.device, &desc);

    WGPUBindGroupLayout multi[3] = { wg.uniformLayout, wg.textureLayout, wg.textureLayout };
    desc.bindGroupLayoutCount = 3;
    desc.bindGroupLayouts = multi;
    wg.layoutMulti = wgpuDeviceCreatePipelineLayout(wg.device, &desc);
}


void wgpu_releasePipelineResources(void)
{
    int i;
    for (i = 0; i < SHADER_COUNT; i++)
    {
        if (s_shaders[i])
            wgpuShaderModuleRelease(s_shaders[i]);
        s_shaders[i] = NULL;
    }
    if (wg.layoutSingle)
        wgpuPipelineLayoutRelease(wg.layoutSingle);
    if (wg.layoutMulti)
        wgpuPipelineLayoutRelease(wg.layoutMulti);
    wg.layoutSingle = NULL;
    wg.layoutMulti = NULL;
}


static WGPUBlendFactor wgpu_srcBlendFactor(uint32_t stateBits)
{
    switch (stateBits & GLS_SRCBLEND_BITS)
    {
        case GLS_SRCBLEND_ZERO:                 return WGPUBlendFactor_Zero;
        case GLS_SRCBLEND_ONE:                  return WGPUBlendFactor_One;
        case GLS_SRCBLEND_DST_COLOR:            return WGPUBlendFactor_Dst;
        case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:  return WGPUBlendFactor_OneMinusDst;
        case GLS_SRCBLEND_SRC_ALPHA:            return WGPUBlendFactor_SrcAlpha;
        case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:  return WGPUBlendFactor_OneMinusSrcAlpha;
        case GLS_SRCBLEND_DST_ALPHA:            return WGPUBlendFactor_DstAlpha;
        case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:  return WGPUBlendFactor_OneMinusDstAlpha;
        case GLS_SRCBLEND_ALPHA_SATURATE:       return WGPUBlendFactor_SrcAlphaSaturated;
        default:
            ri.Error(ERR_DROP, "gapi_createPipeline: invalid src blend state bits\n");
            return WGPUBlendFactor_One;
    }
}


static WGPUBlendFactor wgpu_dstBlendFactor(uint32_t stateBits)
{
    switch (stateBits & GLS_DSTBLEND_BITS)
    {
        case GLS_DSTBLEND_ZERO:                 return WGPUBlendFactor_Zero;
        case GLS_DSTBLEND_ONE:                  return WGPUBlendFactor_One;
        case GLS_DSTBLEND_SRC_COLOR:            return WGPUBlendFactor_Src;
        case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:  return WGPUBlendFactor_OneMinusSrc;
        case GLS_DSTBLEND_SRC_ALPHA:            return WGPUBlendFactor_SrcAlpha;
        case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:  return WGPUBlendFactor_OneMinusSrcAlpha;
        case GLS_DSTBLEND_DST_ALPHA:            return WGPUBlendFactor_DstAlpha;
        case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:  return WGPUBlendFactor_OneMinusDstAlpha;
        default:
            ri.Error(ERR_DROP, "gapi_createPipeline: invalid dst blend state bits\n");
            return WGPUBlendFactor_Zero;
    }
}


// r_apiCoverage heavy: the callback of the asynchronous build. It runs in
// wgpu_resolvePipeline, in wgpuInstanceWaitAny (WGPUCallbackMode_WaitAnyOnly).
static void wgpu_onRenderPipelineCreated(WGPUCreatePipelineAsyncStatus status, WGPURenderPipeline renderPipeline,
        WGPUStringView message, void* userdata1, void* userdata2)
{
    gapi_pipeline_t* pipeline = (gapi_pipeline_t*)userdata1;
    if (status == WGPUCreatePipelineAsyncStatus_Success)
    {
        pipeline->pipeline = renderPipeline;
    }
    else
    {
        char text[1024];
        ri.Printf(PRINT_WARNING, "WebGPU: asynchronous pipeline build failed: %s\n",
                wgpu_cString(message, text, sizeof(text)));
        if (renderPipeline)
            wgpuRenderPipelineRelease(renderPipeline);
    }
}


// Make the WebGPU pipeline. If asyncTarget is not NULL, start an asynchronous
// build for asyncTarget and return NULL (r_apiCoverage heavy).
static WGPURenderPipeline wgpu_createRenderPipeline(const gapi_pipelineDesc_t* def,
        float depthBiasConstant, float depthBiasSlope, gapi_pipeline_t* asyncTarget)
{
    const qboolean multitexture = (def->shaderType != GAPI_SHADER_SINGLE_TEXTURE);
    const qboolean useClipDistance = def->clipPlane && wg.clipDistances;

    // The specialization constants of the Vulkan backend are WGSL override
    // constants with the same ids. Give each stage only the constants that
    // its shader declares.
    double alphaTestFunc = 0;
    if ((def->stateBits & GLS_ATEST_BITS) == 0)
        alphaTestFunc = 0;
    else if (def->stateBits & GLS_ATEST_GT_0)
        alphaTestFunc = 1;
    else if (def->stateBits & GLS_ATEST_LT_80)
        alphaTestFunc = 2;
    else if (def->stateBits & GLS_ATEST_GE_80)
        alphaTestFunc = 3;
    else
        ri.Error(ERR_DROP, "gapi_createPipeline: invalid alpha test state bits\n");

    const double clipPlane = (def->clipPlane && !useClipDistance) ? 1 : 0;
    const double colorOp = (def->shaderType == GAPI_SHADER_MULTI_TEXTURE_ADD) ? 1 : 0;

    WGPUConstantEntry vertexConstants[1] = { WGPU_CONSTANT_ENTRY_INIT };
    vertexConstants[0].key = wgpu_stringView("2");     // clip_plane
    vertexConstants[0].value = clipPlane;

    WGPUConstantEntry fragmentConstants[3] = { WGPU_CONSTANT_ENTRY_INIT, WGPU_CONSTANT_ENTRY_INIT, WGPU_CONSTANT_ENTRY_INIT };
    size_t fragmentConstantCount = 0;
    fragmentConstants[fragmentConstantCount].key = wgpu_stringView("0");   // alpha_test_func
    fragmentConstants[fragmentConstantCount++].value = alphaTestFunc;
    if (multitexture)
    {
        fragmentConstants[fragmentConstantCount].key = wgpu_stringView("1");   // color_op
        fragmentConstants[fragmentConstantCount++].value = colorOp;
    }
    fragmentConstants[fragmentConstantCount].key = wgpu_stringView("2");   // clip_plane
    fragmentConstants[fragmentConstantCount++].value = clipPlane;

    // The vertex streams: positions (vec4 in memory, vec3 in the shader),
    // colors, and one or two texture coordinate streams, each in its own buffer.
    WGPUVertexAttribute attributes[4] = {
        WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT };
    WGPUVertexBufferLayout buffers[4] = {
        WGPU_VERTEX_BUFFER_LAYOUT_INIT, WGPU_VERTEX_BUFFER_LAYOUT_INIT, WGPU_VERTEX_BUFFER_LAYOUT_INIT, WGPU_VERTEX_BUFFER_LAYOUT_INIT };
    static const WGPUVertexFormat formats[4] = {
        WGPUVertexFormat_Float32x3, WGPUVertexFormat_Unorm8x4, WGPUVertexFormat_Float32x2, WGPUVertexFormat_Float32x2 };
    static const uint64_t strides[4] = { 16, 4, 8, 8 };
    const size_t bufferCount = multitexture ? 4 : 3;
    size_t i;
    for (i = 0; i < bufferCount; i++)
    {
        attributes[i].format = formats[i];
        attributes[i].offset = 0;
        attributes[i].shaderLocation = (uint32_t)i;
        buffers[i].stepMode = WGPUVertexStepMode_Vertex;
        buffers[i].arrayStride = strides[i];
        buffers[i].attributeCount = 1;
        buffers[i].attributes = &attributes[i];
    }

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = multitexture ? wg.layoutMulti : wg.layoutSingle;

    if (useClipDistance)
    {
        desc.vertex.module = s_shaders[multitexture ? SHADER_MULTI_CLIP_VERT : SHADER_SINGLE_CLIP_VERT];
    }
    else
    {
        desc.vertex.module = s_shaders[multitexture ? SHADER_MULTI_VERT : SHADER_SINGLE_VERT];
        desc.vertex.constantCount = 1;
        desc.vertex.constants = vertexConstants;
    }
    desc.vertex.entryPoint = wgpu_stringView("main");
    desc.vertex.bufferCount = bufferCount;
    desc.vertex.buffers = buffers;

    desc.primitive.topology = def->linePrimitives ? WGPUPrimitiveTopology_LineList : WGPUPrimitiveTopology_TriangleList;
    // Q3 defaults to clockwise vertex order. The vertex shaders flip Y, so
    // the front face is the same as in the Vulkan backend.
    desc.primitive.frontFace = WGPUFrontFace_CW;
    switch (def->cullType)
    {
        case GAPI_CULL_TWO_SIDED:
            desc.primitive.cullMode = WGPUCullMode_None;
            break;
        case GAPI_CULL_FRONT_SIDED:
            desc.primitive.cullMode = def->mirror ? WGPUCullMode_Front : WGPUCullMode_Back;
            break;
        case GAPI_CULL_BACK_SIDED:
            desc.primitive.cullMode = def->mirror ? WGPUCullMode_Back : WGPUCullMode_Front;
            break;
    }

    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
    depthStencil.depthWriteEnabled = (def->stateBits & GLS_DEPTHMASK_TRUE) ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    if (def->stateBits & GLS_DEPTHTEST_DISABLE)
        depthStencil.depthCompare = WGPUCompareFunction_Always;
    else if (def->stateBits & GLS_DEPTHFUNC_EQUAL)
        depthStencil.depthCompare = WGPUCompareFunction_Equal;
    else
        depthStencil.depthCompare = WGPUCompareFunction_LessEqual;

    WGPUStencilFaceState face;
    face.compare = WGPUCompareFunction_Always;
    face.failOp = WGPUStencilOperation_Keep;
    face.depthFailOp = WGPUStencilOperation_Keep;
    face.passOp = WGPUStencilOperation_Keep;
    depthStencil.stencilReadMask = 0xFF;
    depthStencil.stencilWriteMask = 0;
    if (def->shadowPhase == GAPI_SHADOW_EDGES)
    {
        face.passOp = (def->cullType == GAPI_CULL_FRONT_SIDED) ?
                WGPUStencilOperation_IncrementClamp : WGPUStencilOperation_DecrementClamp;
        depthStencil.stencilWriteMask = 0xFF;
    }
    else if (def->shadowPhase == GAPI_SHADOW_FULLSCREEN_QUAD)
    {
        face.compare = WGPUCompareFunction_NotEqual;
        depthStencil.stencilWriteMask = 0xFF;
    }
    depthStencil.stencilFront = face;
    depthStencil.stencilBack = face;

    if (def->polygonOffset)
    {
        depthStencil.depthBias = (int32_t)depthBiasConstant;
        depthStencil.depthBiasSlopeScale = depthBiasSlope;
        depthStencil.depthBiasClamp = 0.0f;
    }
    desc.depthStencil = &depthStencil;

    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFF;

    WGPUBlendState blend;
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = wg.surfaceFormat;
    target.writeMask = (def->shadowPhase == GAPI_SHADOW_EDGES) ? WGPUColorWriteMask_None : WGPUColorWriteMask_All;
    if (def->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS))
    {
        blend.color.operation = WGPUBlendOperation_Add;
        blend.color.srcFactor = wgpu_srcBlendFactor(def->stateBits);
        blend.color.dstFactor = wgpu_dstBlendFactor(def->stateBits);
        blend.alpha = blend.color;
        target.blend = &blend;
    }

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = s_shaders[multitexture ? SHADER_MULTI_FRAG : SHADER_SINGLE_FRAG];
    fragment.entryPoint = wgpu_stringView("main");
    fragment.constantCount = fragmentConstantCount;
    fragment.constants = fragmentConstants;
    fragment.targetCount = 1;
    fragment.targets = &target;
    desc.fragment = &fragment;

    if (asyncTarget)
    {
        WGPUCreateRenderPipelineAsyncCallbackInfo callback = WGPU_CREATE_RENDER_PIPELINE_ASYNC_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_WaitAnyOnly;
        callback.callback = wgpu_onRenderPipelineCreated;
        callback.userdata1 = asyncTarget;
        asyncTarget->future = wgpuDeviceCreateRenderPipelineAsync(wg.device, &desc, callback);
        asyncTarget->pending = qtrue;
        return NULL;
    }

    wgpu_pushErrorScope();
    WGPURenderPipeline renderPipeline = wgpuDeviceCreateRenderPipeline(wg.device, &desc);
    wgpu_popErrorScope("render pipeline");
    return renderPipeline;
}


gapi_pipeline_t* gapi_createPipeline(const gapi_pipelineDesc_t* desc)
{
    gapi_pipeline_t* pipeline = (gapi_pipeline_t*) calloc(1, sizeof(gapi_pipeline_t));
    if (pipeline == NULL)
    {
        ri.Error(ERR_FATAL, "gapi_createPipeline: out of memory\n");
    }

    pipeline->desc = *desc;
    // The first draw gives the depth bias. Until then, use the r_offset cvars.
    pipeline->depthBiasConstant = ri.Cvar_Get("r_offsetunits", "-2", CVAR_CHEAT)->value;
    pipeline->depthBiasSlope = ri.Cvar_Get("r_offsetfactor", "-1", CVAR_CHEAT)->value;
    // r_apiCoverage heavy: start the build here, and wait for it at the first
    // draw. The level load makes most of the pipelines before the first frame.
    pipeline->pipeline = wgpu_createRenderPipeline(desc, pipeline->depthBiasConstant, pipeline->depthBiasSlope,
            wg.heavyCoverage ? pipeline : NULL);
    return pipeline;
}


void wgpu_resolvePipeline(gapi_pipeline_t* pipeline)
{
    if (!pipeline->pending)
        return;

    WGPUFutureWaitInfo wait = WGPU_FUTURE_WAIT_INFO_INIT;
    wait.future = pipeline->future;
    wgpuInstanceWaitAny(wg.instance, 1, &wait, UINT64_MAX);
    pipeline->pending = qfalse;

    // If the asynchronous build failed, a synchronous build reports the
    // validation error through its error scope.
    if (pipeline->pipeline == NULL)
        pipeline->pipeline = wgpu_createRenderPipeline(&pipeline->desc, pipeline->depthBiasConstant,
                pipeline->depthBiasSlope, NULL);
}


void wgpu_updatePipelineDepthBias(gapi_pipeline_t* pipeline, float constant, float slope)
{
    if (!pipeline->desc.polygonOffset)
        return;
    if (pipeline->depthBiasConstant == constant && pipeline->depthBiasSlope == slope)
        return;

    wgpuRenderPipelineRelease(pipeline->pipeline);
    pipeline->depthBiasConstant = constant;
    pipeline->depthBiasSlope = slope;
    pipeline->pipeline = wgpu_createRenderPipeline(&pipeline->desc, constant, slope, NULL);
}


void gapi_destroyPipeline(gapi_pipeline_t* pipeline)
{
    // The callback writes to pipeline. Wait for it before the free.
    wgpu_resolvePipeline(pipeline);
    if (pipeline->pipeline)
        wgpuRenderPipelineRelease(pipeline->pipeline);
    free(pipeline);
}
