/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_flares.c
#include "ref_import.h"

#include "tr_local.h"
#include "tr_globals.h"
#include "tr_backend.h"
#include "tr_cvar.h"
#include "tr_flares.h"
#include "tr_pipelines.h"
#include "tr_shade_geometry.h"
#include "tr_state_bits.h"
#include "tr_shader.h"

/*
=============================================================================

LIGHT FLARES

A light flare is an effect that takes place inside the eye when bright light
sources are visible.  The size of the flare relative to the screen is nearly
constant, irrespective of distance, but the intensity should be proportional to the
projected area of the light source.

A surface that has been flagged as having a light flare will calculate the depth
buffer value that its midpoint should have when the surface is added.

After all opaque surfaces have been rendered, the depth buffer is read back for
each flare in view.  If the point has not been obscured by a closer surface, the
flare should be drawn.

Surfaces that have a repeated texture should never be flagged as flaring, because
there will only be a single flare added at the midpoint of the polygon.

To prevent abrupt popping, the intensity of the flare is interpolated up and
down as it changes visibility.  This involves scene to scene state, unlike almost
all other aspects of the renderer, and is complicated by the fact that a single
frame may have multiple scenes.

RB_RenderFlares() will be called once per view (twice in a mirrored scene, potentially
up to five or more times in a frame with 3D status bar icons).

=============================================================================

This file is the flare code of ioq3 (renderergl1/tr_flares.c), with these
changes for the graphics API interface:

- An occlusion query replaces the read of the depth buffer. A small quad 24
  units in front of the flare tests the depth, and the result of an earlier
  frame tells if the flare is visible. Thus the flares need occlusion queries
  (gapi_caps_t). Without them, r_flares does nothing.
- A flare is a quad in world space at the flare origin, with the size in pixels
  of the ioq3 screen quad. The flare shader has no depth test.
- Only the main view has flares, not the portal and the mirror views.
- The dynamic lights add no flares, as in the ioq3 code.
*/

#define FLARE_STDCOEFF		"150"
#define MAX_FLARES			GAPI_MAX_OCCLUSION_QUERIES	// the index of a flare is its query index

// flare states maintain visibility over multiple frames for fading
typedef struct flare_s {
	struct		flare_s	*next;		// for active chain

	int			addedFrame;

	int			frameSceneNum;
	void		*surface;
	int			fogNum;

	int			fadeTime;

	qboolean	visible;			// state of last test
	float		drawIntensity;		// may be non 0 even if !visible due to fading

	uint32_t	tag;				// the tag of the queries of this flare
	qboolean	waiting;			// no query result yet: do not remove the flare

	float		eyeZ;

	vec3_t		origin;
	vec3_t		color;
} flare_t;

static flare_t		r_flareStructs[MAX_FLARES];
static flare_t		*r_activeFlares, *r_inactiveFlares;

static float		flareCoeff;
static uint32_t		s_flareTag;
static qboolean		s_flaresSupported;


static qboolean R_FlaresSupported( void ) {
	gapi_caps_t caps;
	gapi_getCaps( &caps );
	return caps.occlusionQueries;
}


/*
==================
R_SetFlareCoeff
==================
*/
static void R_SetFlareCoeff( void ) {

	if(r_flareCoeff->value == 0.0f)
		flareCoeff = atof(FLARE_STDCOEFF);
	else
		flareCoeff = r_flareCoeff->value;
}


/*
==================
R_InitFlareShader

The shader is loaded only with occlusion queries, so that the pipelines of
the other renderers do not change.
==================
*/
void R_InitFlareShader( void ) {
	int index;

	tr.flareShader = NULL;
	if ( !R_FlaresSupported() ) {
		return;
	}

	tr.flareShader = R_FindShader( "flareShader", LIGHTMAP_NONE, qtrue );

	// Hack to make fogging work correctly on flares. Fog colors are calculated
	// in tr_flare.c already. The stage pipelines must be made again for the
	// new state bits.
	if ( !tr.flareShader->defaultShader ) {
		for ( index = 0; index < tr.flareShader->numUnfoggedPasses; index++ ) {
			tr.flareShader->stages[index]->adjustColorsForFog = ACFF_NONE;
			tr.flareShader->stages[index]->stateBits |= GLS_DEPTHTEST_DISABLE;
			create_pipelines_for_each_stage( tr.flareShader->stages[index], tr.flareShader );
		}
	}
}


/*
==================
R_ClearFlares
==================
*/
void R_ClearFlares( void ) {
	int		i;

	memset( r_flareStructs, 0, sizeof( r_flareStructs ) );
	r_activeFlares = NULL;
	r_inactiveFlares = NULL;

	for ( i = 0 ; i < MAX_FLARES ; i++ ) {
		r_flareStructs[i].next = r_inactiveFlares;
		r_inactiveFlares = &r_flareStructs[i];
	}

	s_flaresSupported = R_FlaresSupported();
	R_SetFlareCoeff();
}


static void R_TransformModelToClip( const vec3_t src, const float *modelMatrix, const float *projectionMatrix,
							vec4_t eye, vec4_t dst ) {
	int i;

	for ( i = 0 ; i < 4 ; i++ ) {
		eye[i] =
			src[0] * modelMatrix[ i + 0 * 4 ] +
			src[1] * modelMatrix[ i + 1 * 4 ] +
			src[2] * modelMatrix[ i + 2 * 4 ] +
			1 * modelMatrix[ i + 3 * 4 ];
	}

	for ( i = 0 ; i < 4 ; i++ ) {
		dst[i] =
			eye[0] * projectionMatrix[ i + 0 * 4 ] +
			eye[1] * projectionMatrix[ i + 1 * 4 ] +
			eye[2] * projectionMatrix[ i + 2 * 4 ] +
			eye[3] * projectionMatrix[ i + 3 * 4 ];
	}
}


/*
==================
RB_AddFlare

This is called at surface tesselation time
==================
*/
static void RB_AddFlare( void *surface, int fogNum, vec3_t point, vec3_t color, vec3_t normal ) {
	int				i;
	flare_t			*f;
	vec3_t			local;
	float			d = 1;
	vec4_t			eye, clip;
	uint32_t		tag;
	uint64_t		samples;

	// only the main view has flares (mirrors are portals too)
	if ( backEnd.viewParms.isPortal ) {
		return;
	}

	if(normal && (normal[0] || normal[1] || normal[2]))
	{
		VectorSubtract( backEnd.viewParms.or.origin, point, local );
		VectorNormalize( local );
		d = DotProduct(local, normal);

		// If the viewer is behind the flare don't add it.
		if(d < 0)
			return;
	}

	// if the point is off the screen, don't bother adding it
	// calculate screen coordinates and depth
	R_TransformModelToClip( point, backEnd.or.modelMatrix,
		backEnd.viewParms.projectionMatrix, eye, clip );

	// check to see if the point is behind the viewer or completely off screen
	if ( clip[3] <= 0 ) {
		return;
	}
	for ( i = 0 ; i < 2 ; i++ ) {
		if ( clip[i] >= clip[3] || clip[i] <= -clip[3] ) {
			return;
		}
	}

	// see if a flare with a matching surface and scene exists
	for ( f = r_activeFlares ; f ; f = f->next ) {
		if ( f->surface == surface && f->frameSceneNum == backEnd.viewParms.frameSceneNum ) {
			break;
		}
	}

	// allocate a new one
	if (!f ) {
		if ( !r_inactiveFlares ) {
			// the list is completely full
			return;
		}
		f = r_inactiveFlares;
		r_inactiveFlares = r_inactiveFlares->next;
		f->next = r_activeFlares;
		r_activeFlares = f;

		f->surface = surface;
		f->frameSceneNum = backEnd.viewParms.frameSceneNum;
		f->addedFrame = -1;

		// a new tag: the results of the queries of an earlier flare in this
		// slot do not count
		f->tag = ++s_flareTag;
		f->waiting = qtrue;
		gapi_getOcclusionResult( (uint32_t)( f - r_flareStructs ), &tag, &samples );
	}

	if ( f->addedFrame != backEnd.viewParms.frameCount - 1 ) {
		f->visible = qfalse;
		f->fadeTime = backEnd.refdef.rd.time - 2000;
	}

	f->addedFrame = backEnd.viewParms.frameCount;
	f->fogNum = fogNum;

	VectorCopy(point, f->origin);
	VectorCopy( color, f->color );

	// fade the intensity of the flare down as the
	// light surface turns away from the viewer
	VectorScale( f->color, d, f->color );

	f->eyeZ = eye[2];
}


void RB_SurfaceFlare(srfFlare_t *surf)
{
	if ( r_flares->integer && s_flaresSupported ) {
		RB_AddFlare( surf, tess.fogNum, surf->origin, surf->color, surf->normal );
	}
}


/*
===============================================================================

FLARE BACK END

===============================================================================
*/

// The size in world units at an eye distance that gives a size in pixels.
static float RB_FlareWorldSize( float pixels, float eyeDistance ) {
	const float focal = backEnd.viewParms.viewportWidth * 0.5f / tan( DEG2RAD( backEnd.viewParms.fovX * 0.5f ) );
	return pixels * eyeDistance / focal;
}


// Put a quad that faces the viewer into tess, at the center with a half size.
static void RB_AddFlareQuad( const vec3_t center, float halfSize ) {
	const float *left = backEnd.viewParms.or.axis[1];
	const float *up = backEnd.viewParms.or.axis[2];
	static const float corners[4][2] = { { 1, 1 }, { 1, -1 }, { -1, -1 }, { -1, 1 } };
	const int first = tess.numVertexes;
	int i;

	for ( i = 0 ; i < 4 ; i++ ) {
		float *xyz = tess.xyz[first + i];
		VectorMA( center, corners[i][0] * halfSize, left, xyz );
		VectorMA( xyz, corners[i][1] * halfSize, up, xyz );
		xyz[3] = 1;
		tess.texCoords[first + i][0][0] = ( corners[i][0] + 1 ) * 0.5f;
		tess.texCoords[first + i][0][1] = ( corners[i][1] + 1 ) * 0.5f;
	}
	tess.numVertexes += 4;

	tess.indexes[tess.numIndexes++] = first + 0;
	tess.indexes[tess.numIndexes++] = first + 1;
	tess.indexes[tess.numIndexes++] = first + 2;
	tess.indexes[tess.numIndexes++] = first + 0;
	tess.indexes[tess.numIndexes++] = first + 2;
	tess.indexes[tess.numIndexes++] = first + 3;
}


/*
==================
RB_TestFlare

Use the newest query result of the flare. The ioq3 code reads the depth
buffer here, and accepts the flare when the depth is less than 24 units in
front of it.
==================
*/
static void RB_TestFlare( flare_t *f ) {
	qboolean		visible;
	float			fade;
	uint32_t		tag;
	uint64_t		samples;

	visible = f->visible;
	if ( gapi_getOcclusionResult( (uint32_t)( f - r_flareStructs ), &tag, &samples ) && tag == f->tag ) {
		visible = ( samples > 0 ) ? qtrue : qfalse;
		f->waiting = qfalse;
	}

	if ( visible ) {
		if ( !f->visible ) {
			f->visible = qtrue;
			f->fadeTime = backEnd.refdef.rd.time - 1;
		}
		fade = ( ( backEnd.refdef.rd.time - f->fadeTime ) /1000.0f ) * r_flareFade->value;
	} else {
		if ( f->visible ) {
			f->visible = qfalse;
			f->fadeTime = backEnd.refdef.rd.time - 1;
		}
		fade = 1.0f - ( ( backEnd.refdef.rd.time - f->fadeTime ) / 1000.0f ) * r_flareFade->value;
	}

	if ( fade < 0 ) {
		fade = 0;
	}
	if ( fade > 1 ) {
		fade = 1;
	}

	f->drawIntensity = fade;
}


/*
==================
RB_QueryFlare

Draw a small quad, 2 pixels in each direction, 24 units in front of the flare,
with an occlusion query. Samples that pass the depth test make the flare
visible.
==================
*/
static void RB_QueryFlare( flare_t *f ) {
	const float minDistance = r_znear->value * 2;
	vec3_t		toEye, center;
	float		distance, pull;
	int			i;

	VectorSubtract( backEnd.viewParms.or.origin, f->origin, toEye );
	distance = VectorNormalize( toEye );
	pull = 24;
	if ( distance - pull < minDistance ) {
		pull = distance - minDistance;
	}
	if ( pull < 0 ) {
		pull = 0;
	}
	VectorMA( f->origin, pull, toEye, center );

	tess.numVertexes = 0;
	tess.numIndexes = 0;
	RB_AddFlareQuad( center, RB_FlareWorldSize( 2, distance - pull ) );
	for ( i = 0 ; i < tess.numVertexes ; i++ ) {
		tess.svars.colors[i][0] = 255;
		tess.svars.colors[i][1] = 255;
		tess.svars.colors[i][2] = 255;
		tess.svars.colors[i][3] = 255;
		tess.svars.texcoords[0][i][0] = 0;
		tess.svars.texcoords[0][i][1] = 0;
	}

	gapi_uploadPositionsAndIndexes( tess.xyz, tess.numVertexes, tess.indexes, tess.numIndexes );
	gapi_beginOcclusionQuery( (uint32_t)( f - r_flareStructs ), f->tag );
	RB_ShadeGeometry( g_stdPipelines.flare_test_pipeline, qfalse, GAPI_DEPTH_NORMAL, qtrue );
	gapi_endOcclusionQuery();

	tess.numVertexes = 0;
	tess.numIndexes = 0;
}


/*
 * As flare sizes stay nearly constant with increasing distance we must decrease the intensity
 * to achieve a reasonable visual result. The intensity is ~ (size^2 / distance^2) which can be
 * got by considering the ratio of
 * (flaresurface on screen) : (Surface of sphere defined by flare origin and distance from flare)
 * An important requirement is:
 * intensity <= 1 for all distances.
 *
 * The formula used here to compute the intensity is as follows:
 * intensity = flareCoeff * size^2 / (distance + size*sqrt(flareCoeff))^2
 * As you can see, the intensity will have a max. of 1 when the distance is 0.
 * The coefficient flareCoeff will determine the falloff speed with increasing distance.
 */
static void RB_RenderFlare( flare_t *f ) {
	float			size;
	vec3_t			color;
	int				iColor[3];
	float distance, intensity, factor;
	byte fogFactors[3] = {255, 255, 255};
	int				i;

	// We don't want too big values anyways when dividing by distance.
	if(f->eyeZ > -1.0f)
		distance = 1.0f;
	else
		distance = -f->eyeZ;

	// calculate the flare size..
	size = backEnd.viewParms.viewportWidth * ( r_flareSize->value/640.0f + 8 / distance );

	factor = distance + size * sqrt(flareCoeff);

	intensity = flareCoeff * size * size / (factor * factor);

	VectorScale(f->color, f->drawIntensity * intensity, color);

	// Calculations for fogging
	if(tr.world && f->fogNum > 0 && f->fogNum < tr.world->numfogs)
	{
		tess.numVertexes = 1;
		VectorCopy(f->origin, tess.xyz[0]);
		tess.fogNum = f->fogNum;

		RB_CalcModulateColorsByFog(fogFactors);
		tess.numVertexes = 0;

		// We don't need to render the flare if colors are 0 anyways.
		if(!(fogFactors[0] || fogFactors[1] || fogFactors[2]))
			return;
	}

	iColor[0] = color[0] * fogFactors[0];
	iColor[1] = color[1] * fogFactors[1];
	iColor[2] = color[2] * fogFactors[2];

	// the fog is in the color: no fog pass
	RB_BeginSurface( tr.flareShader, 0 );

	RB_AddFlareQuad( f->origin, RB_FlareWorldSize( size, distance ) );
	for ( i = 0 ; i < tess.numVertexes ; i++ ) {
		tess.vertexColors[i][0] = iColor[0] > 255 ? 255 : iColor[0];
		tess.vertexColors[i][1] = iColor[1] > 255 ? 255 : iColor[1];
		tess.vertexColors[i][2] = iColor[2] > 255 ? 255 : iColor[2];
		tess.vertexColors[i][3] = 255;
	}

	RB_EndSurface();
}


/*
==================
RB_RenderFlares

Because flares are simulating an occular effect, they should be drawn after
everything (all views) in the entire frame has been drawn.

Because of the way portals use the depth buffer to mark off areas, the
needed information would be lost after each view, so we are forced to draw
flares after each view.

The resulting artifact is that flares in mirrors or portals don't dim properly
when occluded by something in the main view, and portal flares that should
extend past the portal edge will be overwritten.
==================
*/
void RB_RenderFlares (void)
{
	flare_t		*f;
	flare_t		**prev;
	qboolean	draw;

	if ( !s_flaresSupported || !r_flares->integer || backEnd.viewParms.isPortal ) {
		return;
	}

	if(r_flareCoeff->modified)
	{
		R_SetFlareCoeff();
		r_flareCoeff->modified = qfalse;
	}

	// Reset currentEntity to world so that any previously referenced entities
	// don't have influence on the rendering of these flares (i.e. RF_ renderer flags).
	backEnd.currentEntity = &tr.worldEntity;
	backEnd.or = backEnd.viewParms.world;
	set_modelview_matrix( backEnd.viewParms.world.modelMatrix );
	updateMVP( qfalse, qfalse, backEnd.viewParms.world.modelMatrix );
	gapi_bindTexture( 0, tr.whiteImage->tex );

	// test each flare in this view with the newest query result, and start
	// the next query
	draw = qfalse;
	prev = &r_activeFlares;
	while ( ( f = *prev ) != NULL ) {
		// throw out any flares that weren't added last frame
		if ( f->addedFrame < backEnd.viewParms.frameCount - 1 ) {
			*prev = f->next;
			f->next = r_inactiveFlares;
			r_inactiveFlares = f;
			continue;
		}

		// don't draw any here that aren't from this scene
		f->drawIntensity = 0;
		if ( f->frameSceneNum == backEnd.viewParms.frameSceneNum ) {
			RB_TestFlare( f );
			if ( f->addedFrame == backEnd.viewParms.frameCount ) {
				RB_QueryFlare( f );
			}
			if ( f->drawIntensity ) {
				draw = qtrue;
			} else if ( !f->waiting ) {
				// this flare has completely faded out, so remove it from the chain
				*prev = f->next;
				f->next = r_inactiveFlares;
				r_inactiveFlares = f;
				continue;
			}
		}

		prev = &f->next;
	}

	if ( !draw || !tr.flareShader ) {
		return;		// none visible
	}

	for ( f = r_activeFlares ; f ; f = f->next ) {
		if ( f->frameSceneNum == backEnd.viewParms.frameSceneNum
			&& f->drawIntensity ) {
			RB_RenderFlare( f );
		}
	}
}
