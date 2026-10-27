#ifndef TR_FLARE_H_
#define TR_FLARE_H_


void RB_SurfaceFlare(srfFlare_t *surf);

// Light flares need occlusion queries (gapi_caps_t). Without them, these
// functions do nothing, and R_InitFlareShader loads no shader.
void R_InitFlareShader(void);
void R_ClearFlares(void);
void RB_RenderFlares(void);


#endif
