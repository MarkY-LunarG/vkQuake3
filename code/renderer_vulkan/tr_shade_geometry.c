#include "tr_shade_geometry.h"
#include "ref_import.h"
#include "tr_globals.h"
#include "tr_cvar.h"
#include "vk_image.h"
#include "vk_pipelines.h"
#include "matrix_multiplication.h"
#include "tr_backend.h"
#include "glConfig.h"
#include "R_PortalPlane.h"
#include "tr_light.h"
#include "tr_shader.h"



static float s_modelview_matrix[16] QALIGN(16);



void set_modelview_matrix(const float mv[16])
{
    memcpy(s_modelview_matrix, mv, 64);
}


const float * getptr_modelview_matrix()
{
    return s_modelview_matrix;
}


void reset_modelview_matrix(void)
{
    Mat4Identity(s_modelview_matrix);
}


// The viewport and scissor rectangle of the next draws: the window in 2D, and
// the view clamped to the window in 3D.
void RB_GetDrawRect(gapi_rect_t* const r)
{
	
    int width, height;
    R_GetWinResolution(&width, &height);
    
    if (backEnd.projection2D)
	{
		r->x = 0;
		r->y = 0;
		r->width = width;
		r->height = height;
	}
	else
	{
		assert(backEnd.viewParms.viewportWidth > 0);
		assert(backEnd.viewParms.viewportHeight > 0);
		r->x = backEnd.viewParms.viewportX;
        r->y = backEnd.viewParms.viewportY;
        r->width = backEnd.viewParms.viewportWidth;
		r->height = backEnd.viewParms.viewportHeight;

        // for draw model in setu manus       
        if (r->x < 0)
		    r->x = 0;
        if (r->y < 0)
		    r->y = 0;
        if (r->x + r->width > width)
		    r->width = width - r->x;
	    if (r->y + r->height > height)
		    r->height = height - r->y;

        // ri.Printf(PRINT_ALL, "(%d, %d, %d, %d)\n",
        // r->x, r->y, r->width, r->height);
    }
}


// Draw the geometry in tess with a pipeline: the positions and indexes from
// the last gapi_uploadPositionsAndIndexes call, and the colors and texture
// coordinates in tess.svars.
void RB_ShadeGeometry(const gapi_pipeline_t* pipeline, qboolean multitexture,
        gapi_depthRange_t depthRange, qboolean indexed)
{
    gapi_draw_t draw;

    draw.colors = (const byte (*)[4]) tess.svars.colors;
    draw.texCoords0 = (const vec2_t*) tess.svars.texcoords[0];
    draw.texCoords1 = multitexture ? (const vec2_t*) tess.svars.texcoords[1] : NULL;
    draw.numVertexes = tess.numVertexes;
    draw.numIndexes = indexed ? tess.numIndexes : 0;
    draw.depthRange = depthRange;
    RB_GetDrawRect(&draw.viewport);
    draw.depthBiasConstant = r_offsetUnits->value;
    draw.depthBiasSlope = r_offsetFactor->value;

    gapi_draw(pipeline, &draw);
}


void updateMVP(qboolean isPortal, qboolean is2D, const float mvMat4x4[16])
{
    gapi_vertexConstants_t constants QALIGN(16);

	if (isPortal)
    {
        // mvp transform + eye transform + clipping plane in eye space
        constants.portal = qtrue;
    
        // Eye space transform.
        MatrixMultiply4x4_SSE(mvMat4x4, backEnd.viewParms.projectionMatrix, constants.mvp);

        // NOTE: backEnd.or.modelMatrix incorporates s_flipMatrix,
        // so it should be taken into account when computing clipping plane too.

		constants.eyeTransform[0] = backEnd.or.modelMatrix[0];
		constants.eyeTransform[1] = backEnd.or.modelMatrix[4];
		constants.eyeTransform[2] = backEnd.or.modelMatrix[8];
		constants.eyeTransform[3] = backEnd.or.modelMatrix[12];

		constants.eyeTransform[4] = backEnd.or.modelMatrix[1];
		constants.eyeTransform[5] = backEnd.or.modelMatrix[5];
		constants.eyeTransform[6] = backEnd.or.modelMatrix[9];
		constants.eyeTransform[7] = backEnd.or.modelMatrix[13];

		constants.eyeTransform[8] = backEnd.or.modelMatrix[2];
		constants.eyeTransform[9] = backEnd.or.modelMatrix[6];
		constants.eyeTransform[10] = backEnd.or.modelMatrix[10];
		constants.eyeTransform[11] = backEnd.or.modelMatrix[14];
	
        // Clipping plane in eye coordinates.
		struct rplane_s eye_plane;

        R_TransformPlane(backEnd.viewParms.or.axis, backEnd.viewParms.or.origin, &eye_plane);
        
        // Apply s_flipMatrix to be in the same coordinate system as the mvp transform.
        
        constants.clipPlane[0] = -eye_plane.normal[1];
		constants.clipPlane[1] =  eye_plane.normal[2];
		constants.clipPlane[2] = -eye_plane.normal[0];
		constants.clipPlane[3] =  eye_plane.dist;


	}
    else
    {
		// mvp transform only
        constants.portal = qfalse;
        float* mvp = constants.mvp;
        
        if (is2D)
        {            
            float width, height;
            R_GetWinResolutionF(&width, &height);

            mvp[0] = 2.0f / width; 
            mvp[1] = 0.0f; 
            mvp[2] = 0.0f;
            mvp[3] = 0.0f;

            mvp[4] = 0.0f; 
            mvp[5] = 2.0f / height; 
            mvp[6] = 0.0f;
            mvp[7] = 0.0f;

            mvp[8] = 0.0f; 
            mvp[9] = 0.0f; 
            mvp[10] = 1.0f; 
            mvp[11] = 0.0f;
            
            mvp[12] = -1.0f; 
            mvp[13] = -1.0f; 
            mvp[14] = 0.0f;
            mvp[15] = 1.0f;
        }
        else
        {
            // update q3's proj matrix (opengl) to vulkan conventions:
            // z - [0, 1] instead of [-1, 1] and invert y direction
            MatrixMultiply4x4_SSE(mvMat4x4, backEnd.viewParms.projectionMatrix, mvp);
        }

    }

    gapi_setVertexConstants(&constants);
}






static void ComputeColors( shaderStage_t *pStage )
{
	int		i, nVerts;
	//
	// rgbGen
	//
	switch ( pStage->rgbGen )
	{
		case CGEN_IDENTITY:
			memset( tess.svars.colors, 0xff, tess.numVertexes * 4 );
			break;
		default:
		case CGEN_IDENTITY_LIGHTING:
			memset( tess.svars.colors, tr.identityLightByte, tess.numVertexes * 4 );
			break;
		case CGEN_LIGHTING_DIFFUSE:
			RB_CalcDiffuseColor( tess.svars.colors );
			break;
		case CGEN_EXACT_VERTEX:
			memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			break;
		case CGEN_CONST:
            
            nVerts = tess.numVertexes;

			for ( i = 0; i < nVerts; i++ )
            {
				memcpy(tess.svars.colors[i], pStage->constantColor, 4);
			}
			break;
		case CGEN_VERTEX:
			if ( tr.identityLight == 1 )
			{
				memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i][0] = tess.vertexColors[i][0] * tr.identityLight;
					tess.svars.colors[i][1] = tess.vertexColors[i][1] * tr.identityLight;
					tess.svars.colors[i][2] = tess.vertexColors[i][2] * tr.identityLight;
					tess.svars.colors[i][3] = tess.vertexColors[i][3];
				}
			}
			break;
		case CGEN_ONE_MINUS_VERTEX:
			if ( tr.identityLight == 1 )
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i][0] = 255 - tess.vertexColors[i][0];
					tess.svars.colors[i][1] = 255 - tess.vertexColors[i][1];
					tess.svars.colors[i][2] = 255 - tess.vertexColors[i][2];
				}
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i][0] = ( 255 - tess.vertexColors[i][0] ) * tr.identityLight;
					tess.svars.colors[i][1] = ( 255 - tess.vertexColors[i][1] ) * tr.identityLight;
					tess.svars.colors[i][2] = ( 255 - tess.vertexColors[i][2] ) * tr.identityLight;
				}
			}
			break;
		case CGEN_FOG:
		{
			fog_t* fog = tr.world->fogs + tess.fogNum;

            nVerts = tess.numVertexes;

			for (i = 0; i < nVerts; i++)
			{
				tess.svars.colors[i][0] = fog->colorRGBA[0];
				tess.svars.colors[i][1] = fog->colorRGBA[1];
				tess.svars.colors[i][2] = fog->colorRGBA[2];
				tess.svars.colors[i][3] = fog->colorRGBA[3];
			}
		}break;
		case CGEN_WAVEFORM:
			RB_CalcWaveColor( &pStage->rgbWave, tess.svars.colors );
			break;
		case CGEN_ENTITY:
			RB_CalcColorFromEntity( tess.svars.colors );
			break;
		case CGEN_ONE_MINUS_ENTITY:
			RB_CalcColorFromOneMinusEntity( tess.svars.colors );
			break;
	}

	//
	// alphaGen
	//
	switch ( pStage->alphaGen )
	{
	case AGEN_SKIP:
		break;
	case AGEN_IDENTITY:
		if ( pStage->rgbGen != CGEN_IDENTITY ) {
			if ( ( pStage->rgbGen == CGEN_VERTEX && tr.identityLight != 1 ) ||
				 pStage->rgbGen != CGEN_VERTEX ) {
				for ( i = 0; i < tess.numVertexes; i++ ) {
					tess.svars.colors[i][3] = 0xff;
				}
			}
		}
		break;
	case AGEN_CONST:
		if ( pStage->rgbGen != CGEN_CONST ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i][3] = pStage->constantColor[3];
			}
		}
		break;
	case AGEN_WAVEFORM:
		RB_CalcWaveAlpha( &pStage->alphaWave, ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_LIGHTING_SPECULAR:
		RB_CalcSpecularAlpha( ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_ENTITY:
		RB_CalcAlphaFromEntity( ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_ONE_MINUS_ENTITY:
		RB_CalcAlphaFromOneMinusEntity( ( unsigned char * ) tess.svars.colors );
		break;
    case AGEN_VERTEX:
		if ( pStage->rgbGen != CGEN_VERTEX ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i][3] = tess.vertexColors[i][3];
			}
		}
        break;
    case AGEN_ONE_MINUS_VERTEX:
        for ( i = 0; i < tess.numVertexes; i++ )
        {
			tess.svars.colors[i][3] = 255 - tess.vertexColors[i][3];
        }
        break;
	case AGEN_PORTAL:
		{
			unsigned char alpha;

			for ( i = 0; i < tess.numVertexes; i++ )
			{
				vec3_t v;

				VectorSubtract( tess.xyz[i], backEnd.viewParms.or.origin, v );
				float len = VectorLength( v );

				len /= tess.shader->portalRange;

				if ( len < 0 )
				{
					alpha = 0;
				}
				else if ( len > 1 )
				{
					alpha = 0xff;
				}
				else
				{
					alpha = len * 0xff;
				}

				tess.svars.colors[i][3] = alpha;
			}
		}
		break;
	}

	//
	// fog adjustment for colors to fade out as fog increases
	//
	if ( tess.fogNum )
	{
		switch ( pStage->adjustColorsForFog )
		{
		case ACFF_MODULATE_RGB:
			RB_CalcModulateColorsByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_MODULATE_ALPHA:
			RB_CalcModulateAlphasByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_MODULATE_RGBA:
			RB_CalcModulateRGBAsByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_NONE:
			break;
		}
	}
}

static void ComputeTexCoords( shaderStage_t *pStage )
{
	uint32_t i;
	uint32_t b;

	for ( b = 0; b < NUM_TEXTURE_BUNDLES; b++ )
    {
		int tm;

		//
		// generate the texture coordinates
		//
		switch ( pStage->bundle[b].tcGen )
		{
            case TCGEN_IDENTITY:
                memset( tess.svars.texcoords[b], 0, sizeof( float ) * 2 * tess.numVertexes );
                break;
            case TCGEN_TEXTURE:
                for ( i = 0 ; i < tess.numVertexes ; i++ ) {
                    tess.svars.texcoords[b][i][0] = tess.texCoords[i][0][0];
                    tess.svars.texcoords[b][i][1] = tess.texCoords[i][0][1];
                }
                break;
            case TCGEN_LIGHTMAP:
                for ( i = 0 ; i < tess.numVertexes ; i++ ) {
                    tess.svars.texcoords[b][i][0] = tess.texCoords[i][1][0];
                    tess.svars.texcoords[b][i][1] = tess.texCoords[i][1][1];
                }
                break;
            case TCGEN_VECTOR:
                for ( i = 0 ; i < tess.numVertexes ; i++ ) {
                    tess.svars.texcoords[b][i][0] = DotProduct( tess.xyz[i], pStage->bundle[b].tcGenVectors[0] );
                    tess.svars.texcoords[b][i][1] = DotProduct( tess.xyz[i], pStage->bundle[b].tcGenVectors[1] );
                }
                break;
            case TCGEN_FOG:
                RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[b] );
                break;
            case TCGEN_ENVIRONMENT_MAPPED:
                RB_CalcEnvironmentTexCoords( ( float * ) tess.svars.texcoords[b] );
                break;
            case TCGEN_BAD:
                return;
		}

		//
		// alter texture coordinates
		//
		for ( tm = 0; tm < pStage->bundle[b].numTexMods ; tm++ )
        {
			switch ( pStage->bundle[b].texMods[tm].type )
			{
			case TMOD_NONE:
				tm = TR_MAX_TEXMODS;		// break out of for loop
				break;

			case TMOD_TURBULENT:
				RB_CalcTurbulentTexCoords( &pStage->bundle[b].texMods[tm].wave, ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_ENTITY_TRANSLATE:
				RB_CalcScrollTexCoords( backEnd.currentEntity->e.shaderTexCoord, ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_SCROLL:
				RB_CalcScrollTexCoords( pStage->bundle[b].texMods[tm].scroll, ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_SCALE:
				RB_CalcScaleTexCoords( pStage->bundle[b].texMods[tm].scale, ( float * ) tess.svars.texcoords[b] );
				break;
			
			case TMOD_STRETCH:
				RB_CalcStretchTexCoords( &pStage->bundle[b].texMods[tm].wave, ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_TRANSFORM:
				RB_CalcTransformTexCoords( &pStage->bundle[b].texMods[tm], ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_ROTATE:
				RB_CalcRotateTexCoords( pStage->bundle[b].texMods[tm].rotateSpeed, ( float * ) tess.svars.texcoords[b] );
				break;

			default:
				ri.Error( ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'\n", pStage->bundle[b].texMods[tm].type, tess.shader->name );
				break;
			}
		}
	}
}



/*
===================
ProjectDlightTexture
Perform dynamic lighting with another rendering pass
===================
*/
static void ProjectDlightTexture( void )
{
	byte	clipBits[SHADER_MAX_VERTEXES];

	if ( !backEnd.refdef.num_dlights ) {
		return;
	}

    uint32_t l;
	for ( l = 0 ; l < backEnd.refdef.num_dlights ; l++ )
    {
		//dlight_t	*dl;

		if ( !( tess.dlightBits & ( 1 << l ) ) ) {
			continue;	// this surface definately doesn't have any of this light
		}
		float* texCoords = tess.svars.texcoords[0][0];
		//colors = tess.svars.colors[0];

		//dl = &backEnd.refdef.dlights[l];
        vec3_t	origin;

		VectorCopy( backEnd.refdef.dlights[l].transformed, origin );


        float radius = backEnd.refdef.dlights[l].radius;
		float scale = 1.0f / radius;
    	float modulate;

        float floatColor[3] = {
		    backEnd.refdef.dlights[l].color[0] * 255.0f,
		    backEnd.refdef.dlights[l].color[1] * 255.0f,
		    backEnd.refdef.dlights[l].color[2] * 255.0f
        };

        uint32_t i;
		for ( i = 0 ; i < tess.numVertexes ; i++, texCoords += 2)
        {
			vec3_t	dist;

			backEnd.pc.c_dlightVertexes++;

			VectorSubtract( origin, tess.xyz[i], dist );
			texCoords[0] = 0.5f + dist[0] * scale;
			texCoords[1] = 0.5f + dist[1] * scale;

			uint32_t clip = 0;
			if ( texCoords[0] < 0.0f ) {
				clip |= 1;
			}
            else if ( texCoords[0] > 1.0f ) {
				clip |= 2;
			}

			if ( texCoords[1] < 0.0f ) {
				clip |= 4;
			}
            else if ( texCoords[1] > 1.0f ) {
				clip |= 8;
			}

			// modulate the strength based on the height and color
			if ( dist[2] > radius )
            {
				clip |= 16;
				modulate = 0.0f;
			}
            else if ( dist[2] < -radius )
            {
				clip |= 32;
				modulate = 0.0f;
			}
            else
            {
				dist[2] = fabs(dist[2]);
				if ( dist[2] < radius * 0.5f )
                {
					modulate = 1.0f;
				}
                else
                {
					modulate = 2.0f * (radius - dist[2]) * scale;
				}
			}
			clipBits[i] = clip;
            
            // += 4 
			tess.svars.colors[i][0] = (floatColor[0] * modulate);
			tess.svars.colors[i][1] = (floatColor[1] * modulate);
			tess.svars.colors[i][2] = (floatColor[2] * modulate);
			tess.svars.colors[i][3] = 255;
		}

      
		// build a list of triangles that need light
		uint32_t numIndexes = 0;
		for ( i = 0 ; i < tess.numIndexes ; i += 3 )
        {
			uint32_t a, b, c;

			a = tess.indexes[i];
			b = tess.indexes[i+1];
			c = tess.indexes[i+2];
			if ( clipBits[a] & clipBits[b] & clipBits[c] ) {
				continue;	// not lighted
			}
			numIndexes += 3;
		}

		if ( numIndexes == 0 ) {
			continue;
		}


		gapi_bindTexture( 0, tr.dlightImage->tex );
		// include GLS_DEPTHFUNC_EQUAL so alpha tested surfaces don't add light where they aren't rendered
		backEnd.pc.c_totalIndexes += numIndexes;
		backEnd.pc.c_dlightIndexes += numIndexes;

		// VULKAN

		RB_ShadeGeometry(g_stdPipelines.dlight_pipelines[backEnd.refdef.dlights[l].additive > 0 ? 1 : 0][tess.shader->cullType][tess.shader->polygonOffset],
                qfalse, GAPI_DEPTH_NORMAL, qtrue);

	}
}



/*
===================
RB_FogPass
Blends a fog texture on top of everything else
===================
*/
static void RB_FogPass( void ) {

	unsigned int i;

	fog_t* fog = tr.world->fogs + tess.fogNum;

    const unsigned int nVerts = tess.numVertexes;
	for (i = 0; i < nVerts; i++)
	{
		tess.svars.colors[i][0] = fog->colorRGBA[0];
		tess.svars.colors[i][1] = fog->colorRGBA[1];
		tess.svars.colors[i][2] = fog->colorRGBA[2];
		tess.svars.colors[i][3] = fog->colorRGBA[3];
	}

	RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[0] );

	gapi_bindTexture( 0, tr.fogImage->tex );

	// VULKAN

    assert(tess.shader->fogPass > 0);
    const gapi_pipeline_t* pipeline = g_stdPipelines.fog_pipelines[tess.shader->fogPass - 1][tess.shader->cullType][tess.shader->polygonOffset];
    RB_ShadeGeometry(pipeline, qfalse, GAPI_DEPTH_NORMAL, qtrue);
}


void RB_StageIteratorGeneric( void )
{
//	shaderCommands_t *input = &tess;

	RB_DeformTessGeometry();

	// call shader function
	//
	// VULKAN
   
    gapi_uploadPositionsAndIndexes(tess.xyz, tess.numVertexes, tess.indexes, tess.numIndexes);

    updateMVP(backEnd.viewParms.isPortal, backEnd.projection2D, 
            getptr_modelview_matrix() );
    

    uint32_t stage = 0;

	for ( stage = 0; stage < MAX_SHADER_STAGES; ++stage )
	{
		if ( NULL == tess.xstages[stage])
		{
			break;
		}

		ComputeColors( tess.xstages[stage] );
		ComputeTexCoords( tess.xstages[stage] );

        // base
        // set state
		//R_BindAnimatedImage( &tess.xstages[stage]->bundle[0] );
        qboolean multitexture = (tess.xstages[stage]->bundle[1].image[0] != NULL);

    {        
	    if ( tess.xstages[stage]->bundle[0].isVideoMap )
        {
		    ri.CIN_RunCinematic(tess.xstages[stage]->bundle[0].videoMapHandle);
		    ri.CIN_UploadCinematic(tess.xstages[stage]->bundle[0].videoMapHandle);
		    goto ENDANIMA;
	    }

        int numAnimaImg = tess.xstages[stage]->bundle[0].numImageAnimations;

        if ( numAnimaImg <= 1 )
        {
		    gapi_bindTexture( 0, tess.xstages[stage]->bundle[0].image[0]->tex );
            //GL_Bind(tess.xstages[stage]->bundle[0].image[0]);
            goto ENDANIMA;
	    }

        // it is necessary to do this messy calc to make sure animations line up
        // exactly with waveforms of the same frequency
	    int index = (int)( tess.shaderTime * tess.xstages[stage]->bundle[0].imageAnimationSpeed * FUNCTABLE_SIZE ) >> FUNCTABLE_SIZE2;
        
        if ( index < 0 ) {
		    index = 0;	// may happen with shader time offsets
	    }

	    index %= numAnimaImg;

	    gapi_bindTexture( 0, tess.xstages[stage]->bundle[0].image[ index ]->tex );
        //GL_Bind(tess.xstages[stage]->bundle[0].image[ index ]);
    }
    
ENDANIMA:
		//
		// do multitexture
		//

		if ( multitexture )
		{
            // DrawMultitextured( input, stage );
            // output = t0 * t1 or t0 + t1

            // t0 = most upstream according to spec
            // t1 = most downstream according to spec
            // this is an ugly hack to work around a GeForce driver
            // bug with multitexture and clip planes


            if ( tess.xstages[stage]->bundle[1].isVideoMap )
            {
                ri.CIN_RunCinematic(tess.xstages[stage]->bundle[1].videoMapHandle);
                ri.CIN_UploadCinematic(tess.xstages[stage]->bundle[1].videoMapHandle);
                goto END_ANIMA2;
            }

            if ( tess.xstages[stage]->bundle[1].numImageAnimations <= 1 ) {
                gapi_bindTexture( 1, tess.xstages[stage]->bundle[1].image[0]->tex );
                goto END_ANIMA2;
            }

            // it is necessary to do this messy calc to make sure animations line up
            // exactly with waveforms of the same frequency
            int index2 = (int)( tess.shaderTime * tess.xstages[stage]->bundle[1].imageAnimationSpeed * FUNCTABLE_SIZE ) >> FUNCTABLE_SIZE2;

            if ( index2 < 0 ) {
                index2 = 0;	// may happen with shader time offsets
            }
	        
            index2 %= tess.xstages[stage]->bundle[1].numImageAnimations;

            gapi_bindTexture( 1, tess.xstages[stage]->bundle[1].image[ index2 ]->tex );

END_ANIMA2:

            if (r_lightmap->integer)
                gapi_bindTexture( 0, tr.whiteImage->tex );
            
            // replace diffuse texture with a white one thus effectively render only lightmap
		}

       
        gapi_depthRange_t depth_range = GAPI_DEPTH_NORMAL;
        if (tess.shader->isSky)
        {
            depth_range = GAPI_DEPTH_ONE;
            if (r_showsky->integer)
                depth_range = GAPI_DEPTH_ZERO;
        }
        else if (backEnd.currentEntity->e.renderfx & RF_DEPTHHACK)
        {
            depth_range = GAPI_DEPTH_WEAPON;
        }
 
        
        if (backEnd.viewParms.isMirror)
        {
            RB_ShadeGeometry(tess.xstages[stage]->mirrorPipeline, multitexture, depth_range, qtrue);
        }
        else if (backEnd.viewParms.isPortal)
        {
            RB_ShadeGeometry(tess.xstages[stage]->portalPipeline, multitexture, depth_range, qtrue);
        }
        else
        {
            RB_ShadeGeometry(tess.xstages[stage]->pipeline, multitexture, depth_range, qtrue);
        }

                
		// allow skipping out to show just lightmaps during development
		if ( r_lightmap->integer && ( tess.xstages[stage]->bundle[0].isLightmap || tess.xstages[stage]->bundle[1].isLightmap ) )
		{
			break;
		}
	}

	// 
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE
		&& !(tess.shader->surfaceFlags & (SURF_NODLIGHT | SURF_SKY) ) ) {
		ProjectDlightTexture();
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass ) {
		RB_FogPass();
	}
}
