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
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_noise.c
#include "tr_local.h"

#define NOISE_SIZE 256
#define NOISE_MASK ( NOISE_SIZE - 1 )

#define VAL( a ) s_noise_perm[ ( a ) & ( NOISE_MASK )]
#define INDEX( x, y, z, t ) VAL( x + VAL( y + VAL( z + VAL( t ) ) ) )

static float s_noise_table[NOISE_SIZE];
static int s_noise_perm[NOISE_SIZE];

inline static float LerpF(float a, float b, float w )
{
    return ( a + (b - a) * w );
}

static float GetNoiseValue( int x, int y, int z, int t )
{
	int index = INDEX( ( int ) x, ( int ) y, ( int ) z, ( int ) t );

	return s_noise_table[index];
}

// The rand() of the C library is different on each platform (glibc, musl in
// Emscripten, MSVC), and shaders with noise waveforms then look different.
// The noise table uses this copy of the glibc generator (TYPE_3, 31 words)
// with the seed 1001, so all platforms get the table of glibc.
#define NOISE_RAND_MAX 2147483647
#define NOISE_RAND_DISCARD 344		// glibc discards the first 310 values after the 34 seed values

void R_NoiseInit( void )
{
	int32_t r[NOISE_RAND_DISCARD + 2 * NOISE_SIZE];
	int i;

	r[0] = 1001;
	for ( i = 1; i < 31; i++ )
	{
		int64_t v = ( 16807LL * r[i - 1] ) % NOISE_RAND_MAX;
		if ( v < 0 )
			v += NOISE_RAND_MAX;
		r[i] = ( int32_t ) v;
	}
	for ( i = 31; i < 34; i++ )
		r[i] = r[i - 31];
	for ( i = 34; i < ( int ) ARRAY_LEN( r ); i++ )
		r[i] = ( int32_t ) ( ( uint32_t ) r[i - 31] + ( uint32_t ) r[i - 3] );

	for ( i = 0; i < NOISE_SIZE; i++ )
	{
		const int a = ( int ) ( ( uint32_t ) r[NOISE_RAND_DISCARD + 2 * i] >> 1 );
		const int b = ( int ) ( ( uint32_t ) r[NOISE_RAND_DISCARD + 2 * i + 1] >> 1 );
		s_noise_table[i] = ( float ) ( ( ( a / ( float ) NOISE_RAND_MAX ) * 2.0 - 1.0 ) );
		s_noise_perm[i] = ( unsigned char ) ( b / ( float ) NOISE_RAND_MAX * 255 );
	}
}

float R_NoiseGet4f( float x, float y, float z, float t )
{
	int i;
	int ix, iy, iz, it;
	float fx, fy, fz, ft;
	float front[4];
	float back[4];
	float fvalue, bvalue, value[2];

	ix = ( int ) floor( x );
	fx = x - ix;
	iy = ( int ) floor( y );
	fy = y - iy;
	iz = ( int ) floor( z );
	fz = z - iz;
	it = ( int ) floor( t );
	ft = t - it;

	for ( i = 0; i < 2; i++ )
	{
		front[0] = GetNoiseValue( ix, iy, iz, it + i );
		front[1] = GetNoiseValue( ix+1, iy, iz, it + i );
		front[2] = GetNoiseValue( ix, iy+1, iz, it + i );
		front[3] = GetNoiseValue( ix+1, iy+1, iz, it + i );

		back[0] = GetNoiseValue( ix, iy, iz + 1, it + i );
		back[1] = GetNoiseValue( ix+1, iy, iz + 1, it + i );
		back[2] = GetNoiseValue( ix, iy+1, iz + 1, it + i );
		back[3] = GetNoiseValue( ix+1, iy+1, iz + 1, it + i );

		fvalue = LerpF( LerpF( front[0], front[1], fx ), LerpF( front[2], front[3], fx ), fy );
		bvalue = LerpF( LerpF( back[0], back[1], fx ), LerpF( back[2], back[3], fx ), fy );

		value[i] = LerpF( fvalue, bvalue, fz );
	}

	return LerpF( value[0], value[1], ft );
}
