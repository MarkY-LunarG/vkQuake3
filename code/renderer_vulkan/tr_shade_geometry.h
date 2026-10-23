#ifndef TR_SHADE_GEOMETRY_H_
#define TR_SHADE_GEOMETRY_H_

#include "graphics_api/gapi.h"

const float * getptr_modelview_matrix(void);
void set_modelview_matrix(const float mv[16]);
void reset_modelview_matrix(void);

void updateMVP(qboolean isPortal, qboolean is2D, const float mvMat4x4[16]);

void RB_GetDrawRect(gapi_rect_t* const r);
void RB_ShadeGeometry(const gapi_pipeline_t* pipeline, qboolean multitexture,
        gapi_depthRange_t depthRange, qboolean indexed);

#endif
