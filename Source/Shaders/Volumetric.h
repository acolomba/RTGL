// Copyright (c) 2022 Sultim Tsyrendashiev
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef VOLUMETRIC_H_
#define VOLUMETRIC_H_

#if !defined( DESC_SET_GLOBAL_UNIFORM ) || !defined( DESC_SET_VOLUMETRIC )
    #error
#endif


#define VOLUMETRIC_DISTANCE_POW 1

vec3 volume_getCenter_T( const ivec3 cell, const mat4 viewprojInv, const vec3 origin )
{
    vec3 local =
        ( vec3( cell ) + 0.5 ) / vec3( VOLUMETRIC_SIZE_X, VOLUMETRIC_SIZE_Y, VOLUMETRIC_SIZE_Z );

    vec4 ndc = {
        local.x * 2.0 - 1.0,
        local.y * 2.0 - 1.0,
        0.1,
        1.0,
    };

    vec4 worldpos = viewprojInv * ndc;
    worldpos.xyz *= safePositiveRcp( worldpos.w );

    vec3 worlddir = safeNormalize2( worldpos.xyz - origin, vec3( 0 ) );

    float n = globalUniform.volumeCameraNear;
    float f = globalUniform.volumeCameraFar;

    float z    = clamp( local.z, 0.0, 1.0 );
    z          = pow( z, VOLUMETRIC_DISTANCE_POW );
    float dist = mix( n, f, z );

    return origin + worlddir * dist;
}

vec3 volume_toSamplePosition_T( const vec3 world, const mat4 viewproj, const vec3 origin )
{
    vec4 ndc = viewproj * vec4( world, 1.0 );
    ndc.xy /= ndc.w;

    float n = globalUniform.volumeCameraNear;
    float f = globalUniform.volumeCameraFar;

    float dist = length( world - origin );
    float z    = ( dist - n ) / ( f - n );
    z          = clamp( z, 0.0, 1.0 );
    z          = pow( z, 1.0 / VOLUMETRIC_DISTANCE_POW );

    return vec3( 
        ndc.x * 0.5 + 0.5,
        ndc.y * 0.5 + 0.5,
        z );
}


vec3 volume_getCenter( const ivec3 cell ) 
{
    return volume_getCenter_T(
        cell, globalUniform.volumeViewProjInv, globalUniform.cameraPosition.xyz );
}
vec3 volume_getCenter_Prev( const ivec3 prevcell )
{
    return volume_getCenter_T(
        prevcell, globalUniform.volumeViewProjInv_Prev, globalUniform.cameraPositionPrev.xyz );
}


ivec3 volume_toCellIndex( const vec3 samplePosition )
{
    return ivec3( samplePosition *
                  vec3( VOLUMETRIC_SIZE_X, VOLUMETRIC_SIZE_Y, VOLUMETRIC_SIZE_Z ) );
}


vec4 volume_sample( const vec3 world ) 
{
    vec3 sp = volume_toSamplePosition_T(
        world, globalUniform.volumeViewProj, globalUniform.cameraPosition.xyz );

    return textureLod( g_volumetric_Sampler, sp, 0.0 );
}

vec4 volume_sample_Prev( const ivec3 curcell )
{
    vec3 curworld = volume_getCenter( curcell );

    vec3 spPrev = volume_toSamplePosition_T(
        curworld, globalUniform.volumeViewProj_Prev, globalUniform.cameraPositionPrev.xyz );

    return textureLod( g_volumetric_Sampler_Prev, spPrev, 0.0 );
}

vec4 volume_sampleDithered( const vec3  world,
                            const float rnd01,
                            const vec3  unitBasis,
                            float       ditherRadius,
                            float       ditherRadiusZ )
{
    vec3 sp = volume_toSamplePosition_T(
        world, globalUniform.volumeViewProj, globalUniform.cameraPosition.xyz );

    // Doom64-RT: THE DEPTH AXIS GETS ITS OWN RADIUS, and it is not a taste
    // setting -- the two axes are not the same kind of error.
    //
    // unitBasis is -sampleHemisphere(), whose z is sqrt(1-u1) and therefore
    // always positive; negated, the depth term is always <= 0. So this never
    // samples DEEPER than the surface, which is deliberate: a deeper tap would
    // read a froxel column belonging to geometry behind the surface, and that is
    // the dark outline dense smoke draws around everything seen through it.
    //
    // The consequence is that depth is a BIAS, not a dither. g_volumetric holds a
    // prefix sum from the camera outward (CmVolumetricProcess.comp), so a
    // consistently shallower sample returns consistently less accumulated
    // in-scattering -- it does not blur the far end of a column, it deletes it.
    // Mean shortfall is E[rnd01] * radius * E[sqrt(1-u)] = 0.33 * radius
    // froxels; at rt_volume_dither 5 with rt_volume_far 60's 0.94 m slices that
    // is 1.56 m, and it is what cut the moon's shafts off in mid-air before they
    // reached the floor (docs/moon-and-sky-leaks.md S5.5).
    //
    // x and y are r*cos(phi), r*sin(phi) -- symmetric about zero, so they blur
    // the shaft's edge without moving it, and they keep the large radius the
    // smoke work chose. Depth only has to break SLICE BANDING, a half-froxel
    // artefact, so a sub-froxel radius covers it with a bias small enough to
    // ignore.
    sp += rnd01 * vec3( ditherRadius, ditherRadius, ditherRadiusZ ) * unitBasis /
          vec3( VOLUMETRIC_SIZE_X, VOLUMETRIC_SIZE_Y, VOLUMETRIC_SIZE_Z );

    // Doom64-RT: the spatial filter, taken HERE and not during the integration.
    //
    // The obvious place is CmVolumetricProcess, and it does not work: that pass
    // writes the very image it reads (store() -> imageStore( g_volumetric )), and
    // it is only safe because each thread reads exactly the one (x,y) column it
    // writes. Reading a neighbour there returns whatever another thread has
    // already put there -- the finished prefix sum instead of raw scattering --
    // so the volume collapses. Tried it: the smoke vanished and the screen got
    // black bands.
    //
    // At SAMPLE time the volume is read-only, so extra taps carry no such
    // hazard. Four of them in the screen plane, one froxel out, which is where
    // the per-cell variance lives; Z is left alone because it carries the puff's
    // depth extent, and blurring along it would undo the anisotropic shape.
    if( globalUniform.volumeSpatialBlur < 0.001 )
    {
        return textureLod( g_volumetric_Sampler, sp, 0.0 );
    }

    const vec2 texel = 1.0 / vec2( VOLUMETRIC_SIZE_X, VOLUMETRIC_SIZE_Y );

    const vec4 c = textureLod( g_volumetric_Sampler, sp, 0.0 );
    const vec4 n = textureLod( g_volumetric_Sampler, sp + vec3( texel.x, 0, 0 ), 0.0 ) +
                   textureLod( g_volumetric_Sampler, sp - vec3( texel.x, 0, 0 ), 0.0 ) +
                   textureLod( g_volumetric_Sampler, sp + vec3( 0, texel.y, 0 ), 0.0 ) +
                   textureLod( g_volumetric_Sampler, sp - vec3( 0, texel.y, 0 ), 0.0 );

    return mix( c, ( c * 2.0 + n ) / 6.0, globalUniform.volumeSpatialBlur );
}


#endif // VOLUMETRIC_H_