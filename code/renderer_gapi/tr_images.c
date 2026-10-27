#include "tr_local.h"
#include "tr_images.h"
#include "R_ImageProcess.h"
#include "tr_globals.h"
#include "tr_cvar.h"
#include "tr_fog.h"
#include "ref_import.h"

// The image cache. Each image_t has a graphics API texture (gapi.h). The
// picmip, the resample to a power of two, the light scale, and the mipmaps
// are done on the CPU here.



#define FILE_HASH_SIZE	1024
static image_t*	hashTable[FILE_HASH_SIZE];

static int generateHashValue( const char *fname )
{
	uint32_t i = 0;
	int	hash = 0;

	while (fname[i] != '\0') {
		char letter = tolower(fname[i]);
		if (letter =='.') break;		// don't include extension
		if (letter =='\\') letter = '/';	// damn path names
		hash+=(long)(letter)*(i+119);
		i++;
	}
	hash &= (FILE_HASH_SIZE-1);
	return hash;
}




image_t* R_CreateImage( const char *name, unsigned char* pic, const uint32_t width, const uint32_t height,
						qboolean isMipMap, qboolean allowPicmip, int glWrapClampMode)
{
    if (strlen(name) >= MAX_QPATH ) {
        ri.Error (ERR_DROP, "CreateImage: \"%s\" is too long\n", name);
    }


    ri.Printf( PRINT_ALL, " Create Image: %s\n", name);
    
    // Create image_t object.

    image_t* pImage = (image_t*) ri.Hunk_Alloc( sizeof( image_t ), h_low );

    strncpy (pImage->imgName, name, sizeof(pImage->imgName));
    pImage->index = tr.numImages;
    pImage->mipmap = isMipMap;
    pImage->mipLevels = 1;
    pImage->allowPicmip = allowPicmip;
    pImage->wrapClampMode = glWrapClampMode;
    pImage->width = width;
    pImage->height = height;
    pImage->isLightmap = (strncmp(name, "*lightmap", 9) == 0);
    // Create corresponding GPU resource, lightmaps are always allocated on TMU 1 .
    // A texture mapping unit (TMU) is a component in modern graphics processing units (GPUs). 
    // Historically it was a separate physical processor. A TMU is able to rotate, resize, 
    // and distort a bitmap image (performing texture sampling), to be placed onto an arbitrary
    // plane of a given 3D model as a texture. This process is called texture mapping. 
    // In modern graphics cards it is implemented as a discrete stage in a graphics pipeline, 
    // whereas when first introduced it was implemented as a separate processor, 
    // e.g. as seen on the Voodoo2 graphics card. 
    //
    // The TMU came about due to the compute demands of sampling and transforming a flat
    // image (as the texture map) to the correct angle and perspective it would need to
    // be in 3D space. The compute operation is a large matrix multiply, 
    // which CPUs of the time (early Pentiums) could not cope with at acceptable performance.
    //
    // Today (2013), TMUs are part of the shader pipeline and decoupled from the
    // Render Output Pipelines (ROPs). For example, in AMD's Cypress GPU, 
    // each shader pipeline (of which there are 20) has four TMUs, giving the GPU 80 TMUs.
    // This is done by chip designers to closely couple shaders and the texture engines
    // they will be working with. 
    //
    // 3D scenes are generally composed of two things: 3D geometry, and the textures 
    // that cover that geometry. Texture units in a video card take a texture and 'map' it
    // to a piece of geometry. That is, they wrap the texture around the geometry and 
    // produce textured pixels which can then be written to the screen. 
    //
    // Textures can be an actual image, a lightmap, or even normal maps for advanced 
    // surface lighting effects. 


    // convert to exact power of 2 sizes
    // GetScaledDimension(width, height, &pImage->uploadWidth, &pImage->uploadHeight, allowPicmip);
  
    const unsigned int max_texture_size = 2048;
    
    unsigned int scaled_width, scaled_height;

    for(scaled_width = max_texture_size; scaled_width > width; scaled_width>>=1)
        ;
    
    for (scaled_height = max_texture_size; scaled_height > height; scaled_height>>=1)
        ;


    if ( allowPicmip )
    {
        scaled_width >>= r_picmip->integer;
        scaled_height >>= r_picmip->integer;
    }

    pImage->uploadWidth = scaled_width;
    pImage->uploadHeight = scaled_height;
    
    uint32_t buffer_size = 4 * pImage->uploadWidth * pImage->uploadHeight;
    unsigned char * const pUploadBuffer = (unsigned char*) malloc ( 2 * buffer_size);

    if ((scaled_width != width) || (scaled_height != height) )
    {
        // just info
        // ri.Printf( PRINT_WARNING, "ResampleTexture: inwidth: %d, inheight: %d, outwidth: %d, outheight: %d\n",
        //        width, height, scaled_width, scaled_height );
        
        //go down from [width, height] to [scaled_width, scaled_height]
        ResampleTexture (pUploadBuffer, width, height, pic, scaled_width, scaled_height);
    }
    else
    {
        memcpy(pUploadBuffer, pic, buffer_size);
    }


    // perform optional picmip operation


    if(isMipMap)
    {
        uint32_t curMipMapLevel = 1; 
        uint32_t base_width = pImage->uploadWidth;
        uint32_t base_height = pImage->uploadHeight;

        unsigned char* in_ptr = pUploadBuffer;
        unsigned char* dst_ptr = in_ptr + buffer_size;

        R_LightScaleTexture(pUploadBuffer, pUploadBuffer, buffer_size);

        // Use the normal mip-mapping to go down from [scaled_width, scaled_height] to [1,1] dimensions.

        while (1)
        {

            if ( r_simpleMipMaps->integer )
            {
                R_MipMap(in_ptr, base_width, base_height, dst_ptr);
            }
            else
            {
                R_MipMap2(in_ptr, base_width, base_height, dst_ptr);
            }


            if ((base_width == 1) && (base_height == 1))
                break;

            base_width >>= 1;
            if (base_width == 0) 
                base_width = 1;

            base_height >>= 1;
            if (base_height == 0)
                base_height = 1;

            uint32_t curLevelSize = base_width * base_height * 4;

            buffer_size += curLevelSize;
            
            if ( r_colorMipLevels->integer ) {
                R_BlendOverTexture( in_ptr, base_width * base_height, curMipMapLevel );
            }


            ++curMipMapLevel;

            in_ptr = dst_ptr;
            dst_ptr += curLevelSize; 
        }
        pImage->mipLevels = curMipMapLevel; 
        // ri.Printf( PRINT_WARNING, "curMipMapLevel: %d, base_width: %d, base_height: %d, buffer_size: %d, name: %s\n",
        //    curMipMapLevel, scaled_width, scaled_height, buffer_size, name);
    }

    
    pImage->tex = gapi_createTexture(pImage->uploadWidth, pImage->uploadHeight, pImage->mipLevels,
            pImage->mipmap, pImage->wrapClampMode == GL_REPEAT ? qtrue : qfalse);
    gapi_setTextureName(pImage->tex, pImage->imgName);
    gapi_uploadTexture(pImage->tex, pUploadBuffer, pImage->mipLevels);


    free(pUploadBuffer);


    const int hash = generateHashValue(name);
    pImage->next = hashTable[hash];
    hashTable[hash] = pImage;

    tr.images[tr.numImages] = pImage;
    if ( ++tr.numImages == MAX_DRAWIMAGES )
    {
        ri.Error( ERR_DROP, "CreateImage: MAX_DRAWIMAGES hit\n");
    }

    return pImage;
}


image_t* R_FindImageFile(const char *name, qboolean mipmap, qboolean allowPicmip, int glWrapClampMode)
{
   	image_t* image;

	if (name == NULL)
    {
        ri.Printf( PRINT_WARNING, "Find Image File: NULL\n");
		return NULL;
	}
    // ri.Printf( PRINT_WARNING, "Find Image File: %s\n", name);

	int hash = generateHashValue(name);

	// see if the image is already loaded

	for (image=hashTable[hash]; image; image=image->next)
	{
		if ( !strcmp( name, image->imgName ) )
		{
			// the white image can be used with any set of parms,
			// but other mismatches are errors
			if ( strcmp( name, "*white" ) )
			{
				if ( image->mipmap != mipmap ) {
					ri.Printf( PRINT_WARNING, "WARNING: reused image %s with mixed mipmap parm\n", name );
				}
				if ( image->allowPicmip != allowPicmip ) {
					ri.Printf( PRINT_WARNING, "WARNING: reused image %s with mixed allowPicmip parm\n", name );
				}
				if ( image->wrapClampMode != glWrapClampMode ) {
					ri.Printf( PRINT_WARNING, "WARNING: reused image %s with mixed glWrapClampMode parm\n", name );
				}
			}
			return image;
		}
	}

	//
	// load the pic from disk
    //
    uint32_t width = 0, height = 0;
    unsigned char* pic = NULL;
    
    if(r_loadImgAPI->integer)
        R_LoadImage2( name, &pic, &width, &height );
    else
        R_LoadImage( name, &pic, &width, &height );
    

    if (pic == NULL)
    {
        ri.Printf( PRINT_WARNING, "R_FindImageFile: Fail loading %s the from disk\n", name);
        return NULL;
    }

    image = R_CreateImage( name, pic, width, height, mipmap, allowPicmip, glWrapClampMode);

    ri.Free( pic );
    
    return image;
}


void RE_UploadCinematic (int w, int h, int cols, int rows, const unsigned char *data, int client, qboolean dirty)
{

    image_t* prtImage = tr.scratchImage[client];
    
    // if the scratchImage isn't in the format we want, specify it as a new texture
    if ( (cols != prtImage->uploadWidth) || (rows != prtImage->uploadHeight) )
    {
        ri.Printf(PRINT_ALL, "w=%d, h=%d, cols=%d, rows=%d, client=%d, prtImage->width=%d, prtImage->height=%d\n", 
           w, h, cols, rows, client, prtImage->uploadWidth, prtImage->uploadHeight);

        prtImage->uploadWidth = cols;
        prtImage->uploadHeight = rows;
        prtImage->mipLevels = 1;

        gapi_destroyTexture(prtImage->tex);
        prtImage->tex = gapi_createTexture(cols, rows, 1, prtImage->mipmap,
                prtImage->wrapClampMode == GL_REPEAT ? qtrue : qfalse);
        gapi_setTextureName(prtImage->tex, prtImage->imgName);
        gapi_uploadTexture(prtImage->tex, data, 1);
    }
    else if (dirty)
    {
        // otherwise, just subimage upload it so that
        // drivers can tell we are going to be changing
        // it and don't try and do a texture compression       
        gapi_uploadTexture(prtImage->tex, data, 1);
    }
}


static void R_CreateDefaultImage( void )
{
	#define	DEFAULT_SIZE	16

	unsigned char data[DEFAULT_SIZE][DEFAULT_SIZE][4];

	// the default image will be a box, to allow you to see the mapping coordinates
	memset( data, 32, sizeof( data ) );

	uint32_t x;
	for ( x = 0; x < DEFAULT_SIZE; x++ )
	{
		data[0][x][0] =
			data[0][x][1] =
			data[0][x][2] =
			data[0][x][3] = 255;

		data[x][0][0] =
			data[x][0][1] =
			data[x][0][2] =
			data[x][0][3] = 255;

		data[DEFAULT_SIZE-1][x][0] =
			data[DEFAULT_SIZE-1][x][1] =
			data[DEFAULT_SIZE-1][x][2] =
			data[DEFAULT_SIZE-1][x][3] = 255;

		data[x][DEFAULT_SIZE-1][0] =
			data[x][DEFAULT_SIZE-1][1] =
			data[x][DEFAULT_SIZE-1][2] =
			data[x][DEFAULT_SIZE-1][3] = 255;
	}
	tr.defaultImage = R_CreateImage("*default", (unsigned char *)data, DEFAULT_SIZE, DEFAULT_SIZE, qtrue, qfalse, GL_REPEAT);
}



static void R_CreateWhiteImage(void)
{
	// we use a solid white image instead of disabling texturing
	unsigned char data[DEFAULT_SIZE][DEFAULT_SIZE][4];
	memset( data, 255, sizeof( data ) );
	tr.whiteImage = R_CreateImage("*white", (unsigned char *)data, 8, 8, qfalse, qfalse, GL_REPEAT);
}


static void R_CreateIdentityLightImage(void)
{
    uint32_t x,y;
	unsigned char data[DEFAULT_SIZE][DEFAULT_SIZE][4];

	// with overbright bits active, we need an image which is some fraction of full color,
	// for default lightmaps, etc
	for (x=0 ; x<DEFAULT_SIZE ; x++) {
		for (y=0 ; y<DEFAULT_SIZE ; y++) {
			data[y][x][0] = 
			data[y][x][1] = 
			data[y][x][2] = tr.identityLightByte;
			data[y][x][3] = 255;
		}
	}
	tr.identityLightImage = R_CreateImage("*identityLight", (unsigned char *)data, 8, 8, qfalse, qfalse, GL_REPEAT);
}


static void R_CreateDlightImage( void )
{
    #define	DLIGHT_SIZE	16

	uint32_t x,y;
	unsigned char data[DLIGHT_SIZE][DLIGHT_SIZE][4];

	// make a centered inverse-square falloff blob for dynamic lighting
	for (x=0; x<DLIGHT_SIZE; x++)
    {
		for (y=0; y<DLIGHT_SIZE; y++)
        {
			float d = ( DLIGHT_SIZE/2 - 0.5f - x ) * ( DLIGHT_SIZE/2 - 0.5f - x ) +
				( DLIGHT_SIZE/2 - 0.5f - y ) * ( DLIGHT_SIZE/2 - 0.5f - y );
			int b = 4000 / d;
			if (b > 255) {
				b = 255;
			} else if ( b < 75 ) {
				b = 0;
			}

			data[y][x][0] = 
			data[y][x][1] = 
			data[y][x][2] = b;
			data[y][x][3] = 255;			
		}
	}
	tr.dlightImage = R_CreateImage("*dlight", (unsigned char *)data, DLIGHT_SIZE, DLIGHT_SIZE, qfalse, qfalse, GL_CLAMP);
}


static void R_CreateFogImage( void )
{
    #define	FOG_S	256
    #define	FOG_T	32

	unsigned int x,y;

	unsigned char* const data = (unsigned char*) malloc( FOG_S * FOG_T * 4 );

	// S is distance, T is depth
	for (x=0 ; x<FOG_S ; x++)
    {
		for (y=0 ; y<FOG_T ; y++)
        {
			float d = R_FogFactor( ( x + 0.5f ) / FOG_S, ( y + 0.5f ) / FOG_T );

            unsigned int index = (y*FOG_S+x)*4;
			data[index ] = 
			data[index+1] = 
			data[index+2] = 255;
			data[index+3] = 255*d;
		}
	}
	// standard openGL clamping doesn't really do what we want -- it includes
	// the border color at the edges.  OpenGL 1.2 has clamp-to-edge, which does
	// what we want.
	tr.fogImage = R_CreateImage("*fog", (unsigned char *)data, FOG_S, FOG_T, qfalse, qfalse, GL_CLAMP);
	
    free( data );
}


static void R_CreateScratchImage(void)
{
    uint32_t x;
    
    unsigned char data[DEFAULT_SIZE][DEFAULT_SIZE][4];

    for(x=0;x<32;x++)
    {
        // scratchimage is usually used for cinematic drawing
        tr.scratchImage[x] = R_CreateImage("*scratch", (unsigned char *)data, DEFAULT_SIZE, DEFAULT_SIZE, qfalse, qtrue, GL_CLAMP);
    }
}


void R_InitImages( void )
{
    memset(hashTable, 0, sizeof(hashTable));

	// setup the overbright lighting

	tr.identityLight = 1.0f;
	tr.identityLightByte = 255 * tr.identityLight;

    // build brightness translation tables
    R_SetColorMappings();

    // create default texture and white texture
    // R_CreateBuiltinImages();

    R_CreateDefaultImage();

    R_CreateWhiteImage();

    R_CreateIdentityLightImage();

    R_CreateScratchImage();
    
    R_CreateDlightImage();
    
    R_CreateFogImage();
}


void R_DestroyImages(void)
{
    uint32_t i = 0;

	for (i = 0; i < tr.numImages; i++)
	{
        ri.Printf(PRINT_ALL, " Destroy Image: %s \n", tr.images[i]->imgName);
        gapi_destroyTexture(tr.images[i]->tex);
        tr.images[i]->tex = NULL;
	}

    memset( tr.images, 0, sizeof( tr.images ) );
    tr.numImages = 0;

    memset(hashTable, 0, sizeof(hashTable));
}
