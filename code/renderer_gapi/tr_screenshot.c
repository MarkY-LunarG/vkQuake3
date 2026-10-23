#include "tr_globals.h"
#include "tr_screenshot.h"

#include "R_ImageProcess.h"
#include "R_ImageJPG.h"
#include "ref_import.h"
#include "glConfig.h"
/* 
============================================================================== 
 
						SCREEN SHOTS 

NOTE TTimo
some thoughts about the screenshots system:
screenshots get written in fs_homepath + fs_gamedir
vanilla q3 .. baseq3/screenshots/ *.tga
team arena .. missionpack/screenshots/ *.tga

two commands: "screenshot" and "screenshotJPEG"
we use statics to store a count and start writing the first screenshot/screenshot????.tga (.jpg) available
(with FS_FileExists / FS_FOpenFileWrite calls)
FIXME: the statics don't get a reinit between fs_game changes



============================================================================== 

*/


static void imgFlipY(unsigned char * pBuf, const uint32_t w, const uint32_t h)
{
    const uint32_t a_row = w * 4;
    const uint32_t nLines = h / 2;

    unsigned char* pTmp = (unsigned char*) malloc( a_row );
    unsigned char *pSrc = pBuf;
    unsigned char *pDst = pBuf + w * (h - 1) * 4;

    uint32_t j = 0;
    for (j = 0; j < nLines; j++)
    {
        memcpy(pTmp, pSrc, a_row );
        memcpy(pSrc, pDst, a_row );
        memcpy(pDst, pTmp, a_row );

        pSrc += a_row;
        pDst -= a_row;
	}

    free(pTmp);
}


// Just reading the pixels for the GPU MEM, don't care about swizzling

extern void RE_SaveJPG(char * filename, int quality, int image_width, int image_height, unsigned char *image_buffer, int padding);



void RB_TakeScreenshot( int width, int height, char *fileName, qboolean isJpeg)
{
    ri.Printf(PRINT_ALL, "read %dx%d pixels from GPU\n", width, height);
    const uint32_t cnPixels = width * height; 

    if(isJpeg)
    {
        
        //unsigned char *buffer;
        //size_t offset = 0, memcount;
        //int padlen;
        //memcount = (width * 3 + padlen) * height;
        //RE_SaveJPG(fileName, 90, width, height, buffer + offset, padlen);
        unsigned char* const pImg = (unsigned char*) malloc ( cnPixels * 4);

        gapi_readPixels(pImg, width, height);
       
        // gapi_readPixels gives the top row first. The JPEG writer and the
        // TGA header expect the bottom row first, as glReadPixels gives.
        imgFlipY(pImg, width, height);

        // Remove alpha channel: RGBA to RGB
        {
            unsigned char* pSrc = pImg;
            unsigned char* pDst = pImg;

            uint32_t i;
            for (i = 0; i < cnPixels; i++)
            {
                pSrc[0] = pDst[0];
                pSrc[1] = pDst[1];
                pSrc[2] = pDst[2];
                pSrc += 3;
                pDst += 4;
            }
        }

        RE_SaveJPG(fileName, 90, width, height, pImg, 0);

        free( pImg );

        //bufSize = RE_SaveJPGToBuffer(out, bufSize, 90, width, height, pImg, padding);
        //ri.FS_WriteFile(filename, out, bufSize);
    }
    else
    {

        //const uint32_t cnPixels = width * height;
        const uint32_t imgSize = 18 + cnPixels * 3;

        unsigned char* const pBuffer = (unsigned char*) malloc ( imgSize + cnPixels * 4 );
        unsigned char* const buffer_ptr = pBuffer + 18;
        unsigned char* const pImg = pBuffer + imgSize;
        
        gapi_readPixels(pImg, width, height);

        // gapi_readPixels gives the top row first. The JPEG writer and the
        // TGA header expect the bottom row first, as glReadPixels gives.
        imgFlipY(pImg, width, height);

        memset (pBuffer, 0, 18);
        pBuffer[2] = 2;		// uncompressed type
        pBuffer[12] = width & 255;
        pBuffer[13] = width >> 8;
        pBuffer[14] = height & 255;
        pBuffer[15] = height >> 8;
        pBuffer[16] = 24;	// pixel size

        // RGBA to BGR, the TGA pixel order
        uint32_t i;
        for (i = 0; i < cnPixels; i++)
        {
            buffer_ptr[i*3]   = *(pImg + i*4 + 2);
            buffer_ptr[i*3+1] = *(pImg + i*4 + 1);
            buffer_ptr[i*3+2] = *(pImg + i*4 );
        }
        ri.FS_WriteFile( fileName, pBuffer, imgSize);
        
        free( pBuffer );
    }
}


static void R_TakeScreenshot( int x, int y, int width, int height, char *name, qboolean jpeg )
{
	static char	fileName[MAX_OSPATH] = {0}; // bad things if two screenshots per frame?
	
    screenshotCommand_t	*cmd = (screenshotCommand_t*) R_GetCommandBuffer(sizeof(*cmd));
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_SCREENSHOT;

	cmd->x = x;
	cmd->y = y;
	cmd->width = width;
	cmd->height = height;
	
    //Q_strncpyz( fileName, name, sizeof(fileName) );

    strncpy(fileName, name, sizeof(fileName));

	cmd->fileName = fileName;
	cmd->jpeg = jpeg;
}



/*
====================
R_LevelShot

levelshots are specialized 128*128 thumbnails for the 
menu system, sampled down from full screen distorted images
====================
*/
static void R_LevelShot( int W, int H )
{
	char checkname[MAX_OSPATH];
	unsigned char* buffer;
	unsigned char* source;
	unsigned char* src;
	unsigned char* dst;
	int			x, y;
	int			r, g, b;
	float		xScale, yScale;
	int			xx, yy;
    int i = 0;
	sprintf( checkname, "levelshots/%s.tga", tr.world->baseName );

	source = (unsigned char*) ri.Hunk_AllocateTempMemory( W * H * 3 );

	buffer = (unsigned char*) ri.Hunk_AllocateTempMemory( 128 * 128*3 + 18);
	memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = 128;
	buffer[14] = 128;
	buffer[16] = 24;	// pixel size

    {
        unsigned char* buffer2 = (unsigned char*) malloc (W * H * 4);
        gapi_readPixels(buffer2, W, H);

        // The TGA rows are bottom first.
        imgFlipY(buffer2, W, H);

        unsigned char* buffer_ptr = source;
        unsigned char* buffer2_ptr = buffer2;
        for (i = 0; i < W * H; i++)
        {
            buffer_ptr[0] = buffer2_ptr[0];
            buffer_ptr[1] = buffer2_ptr[1];
            buffer_ptr[2] = buffer2_ptr[2];
            buffer_ptr += 3;
            buffer2_ptr += 4;
        }
        free(buffer2);
    }

	// resample from source
	xScale = W / 512.0f;
	yScale = H / 384.0f;
	for ( y = 0 ; y < 128 ; y++ ) {
		for ( x = 0 ; x < 128 ; x++ ) {
			r = g = b = 0;
			for ( yy = 0 ; yy < 3 ; yy++ ) {
				for ( xx = 0 ; xx < 4 ; xx++ ) {
					src = source + 3 * ( W * (int)( (y*3+yy)*yScale ) + (int)( (x*4+xx)*xScale ) );
					r += src[0];
					g += src[1];
					b += src[2];
				}
			}
			dst = buffer + 18 + 3 * ( y * 128 + x );
			dst[0] = b / 12;
			dst[1] = g / 12;
			dst[2] = r / 12;
		}
	}

	ri.FS_WriteFile( checkname, buffer, 128 * 128*3 + 18 );

	ri.Hunk_FreeTempMemory( buffer );
	ri.Hunk_FreeTempMemory( source );

	ri.Printf( PRINT_ALL, "Wrote %s\n", checkname );
}

/* 
================== 
R_ScreenShot_f

screenshot
screenshot [silent]
screenshot [levelshot]
screenshot [filename]

Doesn't print the pacifier message if there is a second arg
================== 
*/  
void R_ScreenShot_f (void)
{
	char	checkname[MAX_OSPATH];
	static	int	lastNumber = -1;
	qboolean	silent;

    int W;
    int H;

    R_GetWinResolution(&W, &H);


	if ( !strcmp( ri.Cmd_Argv(1), "levelshot" ) )
    {
		R_LevelShot(W, H);
		return;
	}

	if ( !strcmp( ri.Cmd_Argv(1), "silent" ) )
    {
		silent = qtrue;
	}
    else
    {
		silent = qfalse;
	}


	if ( ri.Cmd_Argc() == 2 && !silent )
    {
		// explicit filename
		snprintf( checkname, sizeof(checkname), "screenshots/%s.tga", ri.Cmd_Argv( 1 ) );
	}
    else
    {
		// scan for a free filename

		// if we have saved a previous screenshot, don't scan again, 
        // because recording demo avis can involve thousands of shots
		if ( lastNumber == -1 ) {
			lastNumber = 0;
		}
		// scan for a free number
		for ( ; lastNumber <= 9999 ; lastNumber++ )
        {
			//R_ScreenshotFilename( lastNumber, checkname );
            
            int	a,b,c,d;

            a = lastNumber / 1000;
            b = lastNumber % 1000 / 100;
            c = lastNumber % 100  / 10;
            d = lastNumber % 10;

            snprintf( checkname, sizeof(checkname), "screenshots/shot%i%i%i%i.tga", a, b, c, d );

            if (!ri.FS_FileExists( checkname ))
            {
                break; // file doesn't exist
            }
		}

		if ( lastNumber >= 9999 )
        {
			ri.Printf (PRINT_ALL, "ScreenShot: Couldn't create a file\n"); 
			return;
 		}

		lastNumber++;
	}

	R_TakeScreenshot( 0, 0, W, H, checkname, qfalse );

	if ( !silent ) {
		ri.Printf (PRINT_ALL, "Wrote %s\n", checkname);
	}
} 


void R_ScreenShotJPEG_f(void)
{
	char		checkname[MAX_OSPATH];
	static	int	lastNumber = -1;
	qboolean	silent;

    int W;
    int H;

    R_GetWinResolution(&W, &H);

	if ( !strcmp( ri.Cmd_Argv(1), "levelshot" ) ) {
		R_LevelShot(W, H);
		return;
	}

	if ( !strcmp( ri.Cmd_Argv(1), "silent" ) ) {
		silent = qtrue;
	} else {
		silent = qfalse;
	}

	if ( ri.Cmd_Argc() == 2 && !silent ) {
		// explicit filename
		snprintf( checkname, sizeof(checkname), "screenshots/%s.jpg", ri.Cmd_Argv( 1 ) );
	} else {
		// scan for a free filename

		// if we have saved a previous screenshot, don't scan
		// again, because recording demo avis can involve
		// thousands of shots
		if ( lastNumber == -1 ) {
			lastNumber = 0;
		}
		// scan for a free number
		for ( ; lastNumber <= 9999 ; lastNumber++ )
        {
            int	a,b,c,d;

            a = lastNumber / 1000;
            lastNumber -= a*1000;
            b = lastNumber / 100;
            lastNumber -= b*100;
            c = lastNumber / 10;
            lastNumber -= c*10;
            d = lastNumber;

            snprintf( checkname, sizeof(checkname), "screenshots/shot%i%i%i%i.jpg"
                    , a, b, c, d );

            if (!ri.FS_FileExists( checkname ))
            {
                break; // file doesn't exist
            }
		}

		lastNumber++;
	}

	R_TakeScreenshot( 0, 0, W, H, checkname, qtrue );

	if ( !silent ) {
		ri.Printf (PRINT_ALL, "Wrote %s\n", checkname);
	}
}




void RB_TakeVideoFrameCmd( const videoFrameCommand_t * const cmd )
{

	size_t				memcount, linelen;
	int				padwidth, avipadwidth, padlen;
	

	linelen = cmd->width * 3;

	// Alignment stuff for glReadPixels
	padwidth = PAD(linelen, 4);
	padlen = padwidth - linelen;
	// AVI line padding
	avipadwidth = PAD(linelen, 4);

		
    unsigned char* const pImg = (unsigned char*) malloc ( cmd->width * cmd->height * 4);
    
    gapi_readPixels(pImg, cmd->width, cmd->height);

    imgFlipY(pImg, cmd->width, cmd->height);

	memcount = padwidth * cmd->height;


	if(cmd->motionJpeg)
	{

        const uint32_t cnPixels = cmd->width * cmd->height;
        unsigned char* pSrc = pImg;
        const unsigned char* pDst = pImg;

        uint32_t i;
        // RGBA to RGB
        for (i = 0; i < cnPixels; i++)
        {
            pSrc[0] = pDst[0];
            pSrc[1] = pDst[1];
            pSrc[2] = pDst[2];
            pSrc += 3;
            pDst += 4;
        }

        
		memcount = RE_SaveJPGToBuffer(cmd->encodeBuffer, linelen * cmd->height,
			90,	cmd->width, cmd->height, pImg, padlen);
        
		ri.CL_WriteAVIVideoFrame(cmd->encodeBuffer, memcount);
	}
	else
	{

        unsigned char* buffer_ptr = cmd->encodeBuffer;
        const unsigned char* buffer2_ptr = pImg;

        // RGBA to BGR, the pixel order of uncompressed AVI frames
        uint32_t i;
        for (i = 0; i < cmd->width * cmd->height; i++)
        {
            buffer_ptr[0] = buffer2_ptr[2];
            buffer_ptr[1] = buffer2_ptr[1];
            buffer_ptr[2] = buffer2_ptr[0];
            buffer_ptr += 3;
            buffer2_ptr += 4;
        }

		ri.CL_WriteAVIVideoFrame(cmd->encodeBuffer, avipadwidth * cmd->height);
	}


    free(pImg);

}


void RE_TakeVideoFrame( int width, int height, unsigned char *captureBuffer, unsigned char *encodeBuffer, qboolean motionJpeg )
{
	if( !tr.registered ) {
		return;
	}

	videoFrameCommand_t	* cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if( !cmd ) {
		return;
	}

	cmd->commandId = RC_VIDEOFRAME;

	cmd->width = width;
	cmd->height = height;
	cmd->captureBuffer = captureBuffer;
	cmd->encodeBuffer = encodeBuffer;
	cmd->motionJpeg = motionJpeg;
}
