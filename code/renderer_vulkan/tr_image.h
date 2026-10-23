#ifndef TR_IMAGE_H_
#define TR_IMAGE_H_

#include "graphics_api/gapi.h"


typedef struct image_s {
	char		imgName[MAX_QPATH];		// game path, including extension
	uint32_t	width, height;				// source image
	uint32_t	uploadWidth, uploadHeight;	// after power of two and picmip but not including clamp to MAX_TEXTURE_SIZE

    uint32_t    index;

    // The graphics API texture. The graphics API owns it.
    gapi_texture_t* tex;

    int			wrapClampMode;		// GL_CLAMP or GL_REPEAT
    qboolean    mipmap;
    uint32_t    mipLevels;			// gl texture binding
    qboolean    allowPicmip;
    qboolean    isLightmap;

	struct image_s*	next;
} image_t;


#endif
