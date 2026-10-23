#ifndef TR_IMAGES_H_
#define TR_IMAGES_H_

#include "tr_image.h"

// work around, will be removed in the future

#ifndef GL_REPEAT
#define GL_REPEAT				0x2901
#endif

#ifndef GL_CLAMP
#define GL_CLAMP				0x2900
#endif

image_t* R_FindImageFile(const char *name, qboolean mipmap, qboolean allowPicmip, int glWrapClampMode);
image_t* R_CreateImage( const char *name, unsigned char* pic, uint32_t width, uint32_t height,
						qboolean mipmap, qboolean allowPicmip, int glWrapClampMode);
void R_DestroyImages(void);

void R_LoadImage(const char *name, unsigned char **pic, uint32_t* width, uint32_t* height );
void R_LoadImage2(const char *name, unsigned char **pic, uint32_t* width, uint32_t* height );

#endif
