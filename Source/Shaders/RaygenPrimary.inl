// Copyright (c) 2021-2022 Sultim Tsyrendashiev
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



// This file was originally a raygen shader. But G-buffer decals are drawn
// on primary surfaces, but not in perfect reflections/refractions. Because
// 

// Must be defined:
// - either RAYGEN_PRIMARY_SHADER or RAYGEN_REFL_REFR_SHADER
// - MATERIAL_MAX_ALBEDO_LAYERS

#if defined(RAYGEN_PRIMARY_SHADER) && defined(RAYGEN_REFL_REFR_SHADER)
    #error Only one of RAYGEN_PRIMARY_SHADER and RAYGEN_REFL_REFR_SHADER must be defined
#endif
#if !defined(RAYGEN_PRIMARY_SHADER) && !defined(RAYGEN_REFL_REFR_SHADER)
    #error RAYGEN_PRIMARY_SHADER or RAYGEN_REFL_REFR_SHADER must be defined
#endif
#ifndef MATERIAL_MAX_ALBEDO_LAYERS
    #error MATERIAL_MAX_ALBEDO_LAYERS is not defined
#endif 
#ifndef MATERIAL_LIGHTMAP_LAYER_INDEX
    #error MATERIAL_LIGHTMAP_LAYER_INDEX is not defined
#endif 


#define DESC_SET_TLAS 0
#define DESC_SET_FRAMEBUFFERS 1
#define DESC_SET_GLOBAL_UNIFORM 2
#define DESC_SET_VERTEX_DATA 3
#define DESC_SET_TEXTURES 4
#define DESC_SET_RANDOM 5
#define DESC_SET_LIGHT_SOURCES 6
#define DESC_SET_CUBEMAPS 7
#define DESC_SET_RENDER_CUBEMAP 8
#define DESC_SET_PORTALS 9
#define LIGHT_SAMPLE_METHOD (LIGHT_SAMPLE_METHOD_NONE)
#include "RaygenCommon.h"

vec2 getMotionVectorForUpscaler(const vec2 motionCurToPrev)
{
    return motionCurToPrev;
}

#define UPSCALER_REACTIVITY_REFLREFR 0.8

vec2 getMotionForInfinitePoint(const ivec2 pix)
{
    // treat as a point with .w=0, i.e. at infinite distance
    vec3 rayDir = getRayDir(getPixelUVWithJitter(pix));

    vec3 viewSpacePosCur   = mat3(globalUniform.view)     * rayDir;
    vec3 viewSpacePosPrev  = mat3(globalUniform.viewPrev) * rayDir;

    vec3 clipSpacePosCur   = mat3(globalUniform.projection)     * viewSpacePosCur;
    vec3 clipSpacePosPrev  = mat3(globalUniform.projectionPrev) * viewSpacePosPrev;

    // don't divide by .w
    vec3 ndcCur            = clipSpacePosCur.xyz;
    vec3 ndcPrev           = clipSpacePosPrev.xyz;

    vec2 screenSpaceCur    = ndcCur.xy  * 0.5 + 0.5;
    vec2 screenSpacePrev   = ndcPrev.xy * 0.5 + 0.5;

    return screenSpacePrev - screenSpaceCur;
}

void storeSky( const ivec2 pix,
               const vec3  rayDir,
               bool        calculateSkyAndStoreToAlbedo,
               const vec3  throughput,
#ifdef RAYGEN_PRIMARY_SHADER
               float firstHitDepthNDC )
#else
               bool wasSplit )
#endif
{
    imageStore( framebufIsSky, pix, ivec4( 1 ) );

    {
        // check if was already in G-buffer after rasterization pass
        vec3 skyColor =
            calculateSkyAndStoreToAlbedo
                ? getSkyAlbedo( rayDir )
                : imageLoad( framebufAlbedo, getRegularPixFromCheckerboardPix( pix ) ).rgb;

        {
            // to hdr
            skyColor = adjustSky( skyColor ) / M_PI;
        }

        imageStore(
            framebufAlbedo, getRegularPixFromCheckerboardPix( pix ), vec4( skyColor, 0.0 ) );
    }

    vec2 m = getMotionForInfinitePoint(pix);

    imageStoreNormal(                       pix, vec3(0.0));
    imageStore(framebufMetallicRoughness,   pix, vec4(0.0));
    imageStore(framebufDepthWorld,          pix, vec4(MAX_RAY_LENGTH * 2.0));
    imageStore(framebufMotion,              pix, vec4(m, 0.0, 0.0));
    imageStore(framebufSurfacePosition,     pix, vec4(SURFACE_POSITION_INCORRECT));
    imageStore(framebufVisibilityBuffer,    pix, vec4(UINT32_MAX));
    imageStore(framebufViewDirection,       pix, vec4(rayDir, 0.0));
    imageStore(framebufScreenEmisRT,        getRegularPixFromCheckerboardPix( pix ), vec4( 0.0 ) );
#ifdef RAYGEN_PRIMARY_SHADER
    imageStore(framebufPrimaryToReflRefr,   pix, uvec4(0, 0, PORTAL_INDEX_NONE, 0));
    imageStore(framebufDepthGrad,           pix, vec4(0.0));
    imageStore(framebufDepthNdc,            getRegularPixFromCheckerboardPix(pix), vec4(clamp(firstHitDepthNDC, 0.0, 1.0)));
    imageStore(framebufMotionDlss,          getRegularPixFromCheckerboardPix(pix), vec4(getMotionVectorForUpscaler(m), 0.0, 0.0));
    imageStore(framebufThroughput,          pix, vec4(throughput, 0.0));
    imageStore(framebufReactivity,          getRegularPixFromCheckerboardPix(pix), vec4(0.0));
#else
    imageStore(framebufThroughput,          pix, vec4(throughput, wasSplit ? 1.0 : -1.0));
    imageStore(framebufReactivity,          getRegularPixFromCheckerboardPix(pix), vec4(UPSCALER_REACTIVITY_REFLREFR));
#endif
}

uint getNewRayMedia(int i, uint prevMedia, uint geometryInstanceFlags, float roughness)
{
    // if camera is not in vacuum, assume that new media is vacuum
    if (i == 0 && globalUniform.cameraMediaType != MEDIA_TYPE_VACUUM)
    {
       return MEDIA_TYPE_VACUUM;
    }

    return getMediaTypeFromFlags(geometryInstanceFlags, roughness);
}

vec3 getWaterNormal(const RayCone rayCone, const vec3 rayDir, const vec3 baseNormal, const vec3 position, bool wasPortal)
{
    const mat3 basis = getONB(baseNormal);
    const vec2 baseUV = vec2(dot(position, basis[0]), dot(position, basis[1])); 


    // how much vertical flow to apply
    float verticality = 1.0 - abs(dot(baseNormal, globalUniform.worldUpVector.xyz));

    // project basis[0] and basis[1] on up vector
    vec2 flowSpeedVertical = 10 * vec2(dot(basis[0], globalUniform.worldUpVector.xyz), 
                                       dot(basis[1], globalUniform.worldUpVector.xyz));

    vec2 flowSpeedHorizontal = vec2(1.0);


    const float uvScale = 0.05 / globalUniform.waterTextureAreaScale;
    vec2 speed0 = uvScale * mix(flowSpeedHorizontal, flowSpeedVertical, verticality) * globalUniform.waterWaveSpeed;
    vec2 speed1 = -0.9 * speed0 * mix(1.0, -0.1, verticality);


    // for texture sampling
    float derivU = globalUniform.waterTextureDerivativesMultiplier * 0.5 * uvScale * getWaterDerivU(rayCone, rayDir, baseNormal);

    // make water sharper if visible through the portal
    if (wasPortal)
    {
        derivU *= 0.1;
    }


    vec2 uv0 = uvScale * baseUV + globalUniform.time * speed0;
    vec3 n0 = getTextureSampleDerivU(globalUniform.waterNormalTextureIndex, uv0, derivU).xyz;
    n0.xy = n0.xy * 2.0 - vec2(1.0);


    vec2 uv1 = 0.8 * uvScale * baseUV + globalUniform.time * speed1;
    vec3 n1 = getTextureSampleDerivU(globalUniform.waterNormalTextureIndex, uv1, derivU).xyz;
    n1.xy = n1.xy * 2.0 - vec2(1.0);


    vec2 uv2 = 0.1 * (uvScale * baseUV + speed0 * sin(globalUniform.time * 0.5));
    vec3 n2 = getTextureSampleDerivU(globalUniform.waterNormalTextureIndex, uv2, derivU).xyz;
    n2.xy = n2.xy * 2.0 - vec2(1.0);


    const float strength = globalUniform.waterWaveStrength;

    const vec3 n = normalize(vec3(0, 0, 1) + strength * (0.25 * n0 + 0.2 * n1 + 0.1 * n2));
    return basis * n;   
}

// Colour the caustic veins tend to at full mask. Pale cyan, not white: white
// crests read as foam/plastic, the D64 flat's brightest texels are blue-white.
const vec3 STYLIZED_WATER_CREST_COLOR = vec3( 0.55, 0.80, 1.00 );

// Doom64-RT: stylized water surface colour.
//
// The physical water path (refract + Beer-Lambert absorption + mirror
// reflection) reads far too "real" for Doom 64, and it is also wrong for these
// maps: D64W2_01 / D64W1_01 are plain FLOOR FLATS, there is no sector under
// them to refract into. So the stylized path keeps the surface opaque and
// rebuilds its look from the flat itself: a deep blue body with the texture's
// own pale caustic veins, shimmering with the animated wave normal.
//
//   texAlbedo   the flat as sampled by the primary pass (near-black navy,
//               veins peak around 0.06 / 0.13 / 0.28 sRGB)
//   waveNormal  animated water normal (getWaterNormal)
//   baseNormal  the surface normal before the waves
// out caustic   0..1 vein mask, reused for the screen-space sheen
vec3 getStylizedWaterAlbedo( const vec3  texAlbedo,
                             const vec3  waveNormal,
                             const vec3  baseNormal,
                             out   float caustic )
{
    // the veins ARE the caustics in the source art: normalize the flat's
    // luminance against the brightest texel so the mask is art-independent
    float veins = clamp( getLuminance( texAlbedo ) /
                             max( 0.0001, globalUniform.stylizedWaterVeinRef ),
                         0.0,
                         1.0 );

    // wave crests: how far the animated normal tilts off the plane
    float tilt    = clamp( length( waveNormal - baseNormal ) * 3.0, 0.0, 1.0 );
    float shimmer = tilt * tilt;

    // veins keep their shape, but breathe with the waves
    caustic = clamp( veins * ( 1.0 + globalUniform.stylizedWaterCaustic * shimmer ), 0.0, 1.0 );

    const vec3 body  = globalUniform.stylizedWaterTint.rgb;
    const vec3 crest = mix( body, STYLIZED_WATER_CREST_COLOR, 0.85 );

    return mix( body, crest, caustic );
}

mat3 lookAt(const vec3 forward, const vec3 worldUp)
{
    vec3 right = cross(forward, worldUp);
    vec3 up = cross(right, forward);

    return mat3(right, up, forward);
}

vec3 getPortalNormal(const vec3 baseNormal, const vec3 inWorldOffset)
{
    if (globalUniform.twirlPortalNormal == 0)
    {
        return -baseNormal;
    }

    float phaseScale = 3;
    float timeScale = 3;
    float waveScale = 0.01;
    float tm = mod(timeScale * globalUniform.time, M_PI * 2);

    const mat3 inLookAt_Plain = lookAt(-baseNormal, globalUniform.worldUpVector.xyz);
    const vec2 localOffset_Plain = vec2(dot(inWorldOffset, inLookAt_Plain[0]), 
                                        dot(inWorldOffset, inLookAt_Plain[1]));

    float distance = length(localOffset_Plain);
    float angle = atan(localOffset_Plain.y, localOffset_Plain.x);

    float phase = sin(phaseScale * sqrt(distance) + angle + tm) + 1.0;
    phase *= waveScale;
    // less weight around center
    phase *= clamp(distance / 20, 0, 1); 

    vec3 localN = { phase, phase, 1.0 };

    return inLookAt_Plain * normalize(localN);
}

bool isBackface( const vec3 normal, const vec3 rayDir )
{
    return dot( normal, -rayDir ) < 0.0;
}

vec3 getNormal( const vec3    position,
                bool          hasNormalMap,
                vec3          normal,
                const RayCone rayCone,
                const vec3    rayDir,
                bool          isWater,
                bool          wasPortal )
{
    // TODO: water normal must be a material property, not refl/refr specific
    if( isWater && !hasNormalMap)
    {
        if( isBackface( normal, rayDir ) )
        {
            normal *= -1;
        }

        normal = getWaterNormal( rayCone, rayDir, normal, position, wasPortal );
        return sanitizeNormal( normal /* TODO: needs to be tri normal */, normal, rayDir );
    }
    else
    {
        if( isBackface( normal, rayDir ) )
        {
            normal *= -1;
        }

        return normalize( normal );
    }
}

#ifdef RAYGEN_PRIMARY_SHADER
vec4 makeNdcCoord(const ivec2 pix_regular, float ndcDepth)
{
    return vec4( ( pix_regular.x + 0.5 ) / float( globalUniform.renderWidth ) * 2.0 - 1.0,
                 ( pix_regular.y + 0.5 ) / float( globalUniform.renderHeight ) * 2.0 - 1.0,
                 ndcDepth,
                 1.0 );
}

bool tryAsFluid( const ivec2 pix, const ivec2 pix_regular, const vec3 cameraRayDir, const float hitDepth )
{
    if( globalUniform.fluidEnabled == 0 )
    {
        return false;
    }

    const float fluidDepthNDC =
        saturate( texelFetch( framebufDepthFluid_Sampler, pix_regular, 0 ).r );
    if( fluidDepthNDC > hitDepth )
    {
        return false;
    }

    // restore world position, motion vectors
    const vec4 ndcCur = makeNdcCoord( pix_regular, fluidDepthNDC );

    vec4 viewSpacePosCur = globalUniform.invProjection * ndcCur;
    viewSpacePosCur /= viewSpacePosCur.w;

          vec3 worldPos         = ( globalUniform.invView * viewSpacePosCur ).xyz;
    const vec4 viewSpacePosPrev = globalUniform.viewPrev * vec4( worldPos, 1.0 );
    const vec4 clipSpacePosPrev = globalUniform.projectionPrev * viewSpacePosPrev;
    const vec3 ndcPrev          = clipSpacePosPrev.xyz / clipSpacePosPrev.w;

    const vec2 screenSpaceCur  = ndcCur.xy * 0.5 + 0.5;
    const vec2 screenSpacePrev = ndcPrev.xy * 0.5 + 0.5;

    const vec2  motionCurToPrev            = ( screenSpacePrev - screenSpaceCur );
    const float fluidDepthLinear           = length( viewSpacePosCur.xyz );
    const float motionDepthLinearCurToPrev = length( viewSpacePosPrev.xyz ) - fluidDepthLinear;

    const vec3 normal =
        decodeNormal( texelFetch( framebufFluidNormal_Sampler, pix_regular, 0 ).r );

    // SHIPPING_HACK: apply some offset to prevent clipping with base surface
    worldPos += -0.005 * cameraRayDir;
    // SHIPPING_HACK

    // clang-format off
    // emulate primary surface
    imageStore( framebufIsSky,              pix,            ivec4( 0 ) );
    imageStore( framebufAlbedo,             pix_regular,    vec4( 1, 1, 1, 0 ) );
    imageStore( framebufScreenEmisRT,       pix_regular,    vec4( 0 ) );
    imageStoreNormal(                       pix,            normal );
    imageStore( framebufMetallicRoughness,  pix,            vec4( 1 /* metallic */, 0 /* roughness */, 0, 0 ) );
    imageStore( framebufDepthWorld,         pix,            vec4( fluidDepthLinear ) );
    imageStore( framebufDepthGrad,          pix,            vec4( 0 ) );
    imageStore( framebufMotion,             pix,            vec4( motionCurToPrev, motionDepthLinearCurToPrev, 0.0 ) );
    imageStore( framebufSurfacePosition,    pix,            vec4( worldPos, 0 /* instCustomIndex */ ) );
    imageStore( framebufVisibilityBuffer,   pix,            packVisibilityBuffer_Invalid() );
    imageStore( framebufViewDirection,      pix,            vec4( cameraRayDir, 0.0 ) );
    imageStore( framebufThroughput,         pix,            vec4( globalUniform.fluidColor.xyz, 0.0 ) );
    imageStore( framebufPrimaryToReflRefr,  pix,            uvec4( GEOM_INST_FLAG_REFRACT | GEOM_INST_FLAG_REFLECT | GEOM_INST_FLAG_MEDIA_TYPE_GLASS, 0 /* no instIdAndIndex */, 0, 0 ) );
    imageStore( framebufDepthNdc,           pix_regular,    vec4( clamp( fluidDepthNDC, 0.0, 1.0 ) ) );
    imageStore( framebufMotionDlss,         pix_regular,    vec4( getMotionVectorForUpscaler( motionCurToPrev ), 0.0, 0.0 ) );
    imageStore( framebufReactivity,         pix_regular,    vec4( 0.0 ) );
    // clang-format on

    return true;
}

void main() 
{
    const ivec2 regularPix = ivec2(gl_LaunchIDEXT.xy);
    const ivec2 pix = getCheckerboardPix(regularPix);
    const vec2 inUV = getPixelUVWithJitter(regularPix);

    const vec3 cameraOrigin = globalUniform.cameraPosition.xyz;
    const vec3 cameraRayDir = getRayDir(inUV);
    const vec3 cameraRayDirAX = getRayDirAX(inUV);
    const vec3 cameraRayDirAY = getRayDirAY(inUV);

    // Note: offset a bit, so upscalers do not fail on the edge
    if( classicShading( ivec2( regularPix.x + 16, regularPix.y ) ) ||
        globalUniform.lightmapScreenCoverage >= 0.999 )
    {
        storeSky( pix,
                  cameraRayDir,
                  globalUniform.skyType != SKY_TYPE_RASTERIZED_GEOMETRY,
                  vec3( 1.0 ),
                  1.0 );
        return;
    }


    const ShPayload primaryPayload = tracePrimaryRay(cameraOrigin, cameraRayDir);


    const uint currentRayMedia = globalUniform.cameraMediaType;


    // was no hit
    if (!doesPayloadContainHitInfo(primaryPayload))
    {
        if( tryAsFluid( pix, regularPix, cameraRayDir, 0.999999 ) )
        {
            return;
        }

        vec3 throughput = vec3(1.0);
        // throughput *= getMediaTransmittance(currentRayMedia, pow(abs(dot(cameraRayDir, globalUniform.worldUpVector.xyz)), -3));

        // if sky is a rasterized geometry, it was already rendered to albedo framebuf 
        storeSky( pix,
                  cameraRayDir,
                  globalUniform.skyType != SKY_TYPE_RASTERIZED_GEOMETRY,
                  throughput,
                  1.0 );
        return;
    }


    vec2  motionCurToPrev;
    float motionDepthLinearCurToPrev;
    vec2  gradDepth;
    float firstHitDepthNDC;
    float firstHitDepthLinear;
    vec3  screenEmission;
    const ShHitInfo h = getHitInfoPrimaryRay( primaryPayload,
                                              cameraOrigin,
                                              cameraRayDir,
                                              cameraRayDirAX,
                                              cameraRayDirAY,
                                              motionCurToPrev,
                                              motionDepthLinearCurToPrev,
                                              gradDepth,
                                              firstHitDepthNDC,
                                              firstHitDepthLinear,
                                              screenEmission );

    if( tryAsFluid( pix, regularPix, cameraRayDir, firstHitDepthNDC ) )
    {
        return;
    }

    vec3 throughput = vec3(1.0);
    throughput *= getMediaTransmittance(currentRayMedia, firstHitDepthLinear);


    imageStore(framebufIsSky,               pix, ivec4(0));
    imageStore(framebufAlbedo,              getRegularPixFromCheckerboardPix(pix), vec4(h.albedo, 0.0));
    imageStore(framebufScreenEmisRT,        getRegularPixFromCheckerboardPix(pix), vec4(screenEmission * throughput , 0.0));
    imageStoreNormal(                       pix, h.normal);
    imageStore(framebufMetallicRoughness,   pix, vec4(h.metallic, h.roughness, 0, 0));
    imageStore(framebufDepthWorld,          pix, vec4(firstHitDepthLinear));
    // depth gradients is not 2d, to remove vertical/horizontal artifacts
    imageStore(framebufDepthGrad,           pix, vec4(length(gradDepth)));
    imageStore(framebufMotion,              pix, vec4(motionCurToPrev, motionDepthLinearCurToPrev, 0.0));
    imageStore(framebufSurfacePosition,     pix, vec4(h.hitPosition, uintBitsToFloat(h.instCustomIndex)));
    imageStore(framebufVisibilityBuffer,    pix, packVisibilityBuffer(primaryPayload));
    imageStore(framebufViewDirection,       pix, vec4(cameraRayDir, 0.0));
    imageStore(framebufThroughput,          pix, vec4(throughput, 0.0));

    // save some info for refl/refr shader
    imageStore(framebufPrimaryToReflRefr,   pix, uvec4(h.geometryInstanceFlags, primaryPayload.instIdAndIndex, h.portalIndex, 0));

    // save info for rasterization and upscalers (FSR/DLSS), but only about primary surface,
    // as reflections/refraction only may be losely represented via rasterization
    imageStore(framebufDepthNdc,            getRegularPixFromCheckerboardPix(pix), vec4(clamp(firstHitDepthNDC, 0.0, 1.0)));
    imageStore(framebufMotionDlss,          getRegularPixFromCheckerboardPix(pix), vec4(getMotionVectorForUpscaler(motionCurToPrev), 0.0, 0.0));
    imageStore(framebufReactivity,          getRegularPixFromCheckerboardPix(pix), vec4(0.0));
}
#endif


#ifdef RAYGEN_REFL_REFR_SHADER
void main() 
{
    if (globalUniform.reflectRefractMaxDepth == 0)
    {
        return;
    }


    const ivec2 regularPix = ivec2(gl_LaunchIDEXT.xy);
    const ivec2 pix = getCheckerboardPix(regularPix);
    const vec2 inUV = getPixelUVWithJitter(regularPix);

    const vec3 cameraRayDir = getRayDir(inUV);
    
    if (isSkyPix(pix))
    {
        return;
    }



    // restore state from primary shader
    const uvec3 primaryToReflRefrBuf        = texelFetch(framebufPrimaryToReflRefr_Sampler, pix, 0).rgb;
    ShHitInfo h;
    h.albedo                                = texelFetch(framebufAlbedo_Sampler, getRegularPixFromCheckerboardPix(pix), 0).rgb;
    h.hitPosition                           = texelFetch(framebufSurfacePosition_Sampler, pix, 0).xyz;
    h.geometryInstanceFlags                 = primaryToReflRefrBuf.r;
    h.portalIndex                           = primaryToReflRefrBuf.b;
    h.normal                                = texelFetchNormal(pix);
    {
        vec2 mr                             = texelFetch( framebufMetallicRoughness_Sampler, pix, 0 ).rg;
        h.metallic                          = mr.r;
        h.roughness                         = mr.g;
    }
    const vec3  motionBuf                   = texelFetch(framebufMotion_Sampler, pix, 0).rgb;
    vec2        motionCurToPrev             = motionBuf.rg;
    float       motionDepthLinearCurToPrev  = motionBuf.b;
    float       firstHitDepthLinear         = texelFetch(framebufDepthWorld_Sampler, pix, 0).r;
    vec3        screenEmission              = texelFetch(framebufScreenEmisRT_Sampler, getRegularPixFromCheckerboardPix(pix), 0).rgb;
    vec3        throughput                  = texelFetch(framebufThroughput_Sampler, pix, 0).rgb;
    bool        hasNormalMap                = false; // hack for water normal...
    ShPayload currentPayload;
    currentPayload.instIdAndIndex           = primaryToReflRefrBuf.g;



    RayCone rayCone;
    rayCone.width = 0;
    rayCone.spreadAngle = globalUniform.cameraRayConeSpreadAngle;

    float fullPathLength = firstHitDepthLinear;
    bool wasSplit = false;
    bool wasPortal = false;
    vec3 virtualPos = h.hitPosition;
    vec3 rayDir = cameraRayDir;
    uint currentRayMedia = globalUniform.cameraMediaType;
    // if there was no hitinfo from refl/refr, preserve primary hitinfo
    bool hitInfoWasOverwritten = false;


    propagateRayCone(rayCone, firstHitDepthLinear);



    for (int i = 0; i < globalUniform.reflectRefractMaxDepth; i++)
    {
        const uint instIndex = unpackInstanceIdAndCustomIndex(currentPayload.instIdAndIndex).y;


        bool isPixOdd = isCheckerboardPixOdd(pix) != 0;


        uint newRayMedia =
            getNewRayMedia( i, currentRayMedia, h.geometryInstanceFlags, h.roughness );

        bool isPortal =
            isPortalFromFlags( h.geometryInstanceFlags ) && h.portalIndex != PORTAL_INDEX_NONE;
        bool toRefract = isRefractFromFlags( h.geometryInstanceFlags, h.roughness );
        bool toReflect = isReflectFromFlags( h.geometryInstanceFlags, h.roughness );


        if (!toReflect && !toRefract && !isPortal)
        {
            break;
        }


        const float curIndexOfRefraction = getIndexOfRefraction(currentRayMedia);
        const float newIndexOfRefraction = getIndexOfRefraction(newRayMedia);

        const bool isWater =
            !isPortal && ( newRayMedia == MEDIA_TYPE_WATER || currentRayMedia == MEDIA_TYPE_WATER ||
                           newRayMedia == MEDIA_TYPE_ACID || currentRayMedia == MEDIA_TYPE_ACID );

        const vec3 normal =
            getNormal( h.hitPosition, hasNormalMap, h.normal, rayCone, rayDir, isWater, wasPortal );

        // Doom64-RT: stylized water. Only the FIRST hit, only a vacuum->water
        // crossing (i.e. looking at the surface from above, camera not
        // submerged); everything else keeps the stock physical behaviour.
        const bool stylizedWater = ( globalUniform.stylizedWaterStrength > 0.0 ) &&    //
                                   isWater && i == 0 &&                               //
                                   currentRayMedia == MEDIA_TYPE_VACUUM &&             //
                                   newRayMedia == MEDIA_TYPE_WATER;

        // Doom64-RT diagnostic (rt_water_debug). Three outcomes, one glance:
        //   magenta -- the stylized branch is running
        //   green   -- RTGL sees this surface as water, but the stylized gate
        //              rejected it (media/bounce conditions)
        //   nothing -- the primitive never got RG_MESH_PRIMITIVE_WATER, so the
        //              JSON meta never reached it (tools/set_water_meta.py)
        // Written as screen emission so it is visible with no lighting at all.
        if( globalUniform.stylizedWaterDebug > 0.0 && isWater )
        {
            const ivec2 regPix = getRegularPixFromCheckerboardPix( pix );
            imageStore( framebufAlbedo, regPix, vec4( 0.0 ) );
            imageStore( framebufScreenEmisRT,
                        regPix,
                        vec4( stylizedWater ? vec3( 1, 0, 1 ) : vec3( 0, 1, 0 ), 0.0 ) );
            imageStoreNormal( pix, normal );
            // alpha -1: refl/refr WITHOUT a split, so no checkerboard resolve
            imageStore( framebufThroughput, pix, vec4( vec3( 1.0 ), -1.0 ) );
            return;
        }

        vec3  rayOrigin = h.hitPosition;
        bool  doSplit   = !wasSplit;
        bool  doRefraction;
        vec3  refractionDir;
        float F;

        if( stylizedWater )
        {
            // Never refract: these are opaque floor flats, there is nothing
            // below them to see. Instead spend the checkerboard split on
            //   odd  pixels: keep the water SURFACE in the G-buffer, so it is
            //                lit like any other opaque surface;
            //   even pixels: the mirror reflection, as usual.
            // CmCheckerboard resolves the two halves into
            //   F * reflection + (1 - F) * lit water surface,
            // which is what makes it read as deep blue looking down and
            // reflective at grazing angles.
            doRefraction = false;

            F = min( getFresnelSchlick( curIndexOfRefraction, newIndexOfRefraction, -rayDir, normal ),
                     globalUniform.stylizedWaterReflMax );

            if( isPixOdd )
            {
                float caustic;
                // the base normal must be the one getNormal() actually built
                // the waves around, or the wave-tilt term reads ~2 everywhere
                const vec3 baseNormal =
                    isBackface( h.normal, rayDir ) ? -h.normal : h.normal;
                const vec3 surfAlbedo =
                    getStylizedWaterAlbedo( h.albedo, normal, baseNormal, caustic );

                // *2 compensates the split: this half covers two pixels
                throughput *= ( 1.0 - F ) * 2.0;

                // a little unlit sheen so the caustic pattern still reads in
                // rooms the path tracer leaves nearly black (the original flat
                // was drawn bright); purely on-screen, casts no light
                const vec3 sheen = STYLIZED_WATER_CREST_COLOR * caustic *
                                   globalUniform.stylizedWaterGlow;

                const ivec2 regPix = getRegularPixFromCheckerboardPix( pix );

                // Keep position / depth / motion / visibility from the primary
                // pass -- they already describe this exact surface. Only the
                // shading inputs change.
                imageStore( framebufAlbedo, regPix, vec4( surfAlbedo, 0.0 ) );
                imageStore( framebufScreenEmisRT, regPix, vec4( screenEmission + sheen, 0.0 ) );
                imageStoreNormal( pix, normal );
                imageStore( framebufMetallicRoughness,
                            pix,
                            vec4( 0.0, globalUniform.stylizedWaterRoughness, 0, 0 ) );
                // alpha == 1: was refl/refr WITH a split -> resolve checkerboard
                imageStore( framebufThroughput, pix, vec4( throughput, 1.0 ) );
                return;
            }

            // even pixels fall through to the reflection branch below
        }
        else if (toRefract && calcRefractionDirection(curIndexOfRefraction, newIndexOfRefraction, rayDir, normal, refractionDir))
        {
            doRefraction = isPixOdd;
            F = getFresnelSchlick(curIndexOfRefraction, newIndexOfRefraction, -rayDir, normal);
        }
        else
        {
            // total internal reflection
            doRefraction = false;
            doSplit = false;
            F = 1.0;
        }
        
        if (doRefraction)
        {
            rayDir = refractionDir;
            throughput *= (1 - F);

            if( ( h.geometryInstanceFlags & GEOM_INST_FLAG_THIN_MEDIA ) != 0 )
            {
                // simulate media, knowing its width along normal
                float len = globalUniform.thinMediaWidth / max( 0.001, -dot( normal, rayDir ) );

                rayOrigin += rayDir * len;
                throughput *= getMediaTransmittance( newRayMedia, len );
                fullPathLength += len;
                
                // change media back
                if( calcRefractionDirection( newIndexOfRefraction,
                                             curIndexOfRefraction,
                                             rayDir,
                                             normal,
                                             refractionDir ) )
                {
                    rayDir = refractionDir;
                }
                newRayMedia = currentRayMedia;
            }

            // change media
            currentRayMedia = newRayMedia;
        }
        else if (isPortal)
        {
            const ShPortalInstance portal = g_portals[h.portalIndex];

            const vec3 inCenter = portal.inPosition.xyz;
            const vec3 inWorldOffset = h.hitPosition - inCenter;

            mat3 inLookAt = lookAt(getPortalNormal(normal, inWorldOffset), globalUniform.worldUpVector.xyz);

            const vec3 outCenter = portal.outPosition.xyz;
            const mat3 outLookAt = lookAt(portal.outDirection.xyz, 
                                          portal.outUp.xyz);

            // to local space; then to world space but at portal output
            rayDir = outLookAt * (transpose(inLookAt) * rayDir);

            const vec2 localOffset = vec2(dot(inWorldOffset, inLookAt[0]), 
                                          dot(inWorldOffset, inLookAt[1]));

            rayOrigin = outCenter + localOffset.x * outLookAt[0] + localOffset.y * outLookAt[1];

            wasPortal = true;
        }
        else
        {
            rayDir = reflect( normalize( rayDir ), normal );

            if( !isWater )
            {
                throughput *= getFresnelSchlick( max( 0, dot( normal, rayDir ) ),
                                                 getSpecularColor( h.albedo, h.metallic ) );
            }
            else
            {
                throughput *= F;

    #if SHIPPING_HACK
                rayOrigin += rayDir * 0.005;
    #endif
            }
        }

        if (doSplit)
        {
            throughput *= 2;
            wasSplit = true;
        }


        currentPayload = traceReflectionRefractionRay(rayOrigin, rayDir, instIndex, h.geometryInstanceFlags, doRefraction);

        
        if (!doesPayloadContainHitInfo(currentPayload))
        {
            throughput *= getMediaTransmittance(currentRayMedia, pow(abs(dot(rayDir, globalUniform.worldUpVector.xyz)), -3));

            storeSky(pix, rayDir, true, throughput, wasSplit);
            return;  
        }

        float rayLen;
        vec3 scrEmis;

        h = getHitInfoWithRayCone_ReflectionRefraction(
            currentPayload, rayCone, 
            rayOrigin, rayDir, cameraRayDir, 
            virtualPos, 
            rayLen, 
            motionCurToPrev, motionDepthLinearCurToPrev,
            scrEmis,
            hasNormalMap
        );


        hitInfoWasOverwritten = true;
        throughput *= getMediaTransmittance(currentRayMedia, rayLen);
        propagateRayCone(rayCone, rayLen);
        fullPathLength += rayLen;
        screenEmission += scrEmis * throughput;
    }


    if (!hitInfoWasOverwritten)
    {
        return;
    }


    imageStore(framebufIsSky,               pix, ivec4(0));
    imageStore(framebufAlbedo,              getRegularPixFromCheckerboardPix(pix), vec4(h.albedo, 0.0));
    imageStore(framebufScreenEmisRT,        getRegularPixFromCheckerboardPix(pix), vec4(screenEmission, 0.0));
    imageStoreNormal(                       pix, h.normal);
    imageStore(framebufMetallicRoughness,   pix, vec4(h.metallic, h.roughness, 0, 0));
    imageStore(framebufDepthWorld,          pix, vec4(fullPathLength));
    imageStore(framebufMotion,              pix, vec4(motionCurToPrev, motionDepthLinearCurToPrev, 0.0));
    imageStore(framebufSurfacePosition,     pix, vec4(h.hitPosition, uintBitsToFloat(h.instCustomIndex)));
    imageStore(framebufVisibilityBuffer,    pix, packVisibilityBuffer(currentPayload));
    imageStore(framebufViewDirection,       pix, vec4(rayDir, 0.0));
    imageStore(framebufThroughput,          pix, vec4(throughput, wasSplit ? 1.0 : -1.0));
    imageStore(framebufReactivity,          getRegularPixFromCheckerboardPix(pix), vec4(UPSCALER_REACTIVITY_REFLREFR));
}
#endif