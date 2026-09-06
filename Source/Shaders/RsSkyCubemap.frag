// Doom64-RT: the sky drawn into the 256^2 GI CUBEMAP -- what every ray that
// misses the level samples (getSkyAlbedo). Same compositing as RsSky.frag, so
// the clouds light the level through GI exactly as they appear on screen; the
// only difference is where the world direction comes from. There is no single
// camera here (six multiview faces), so the vertex stage hands down the
// sky-space position and the direction is simply its normalisation: the sky
// viewer sits at the origin of sky space.

#version 460

layout (location = 0) in vec4 vertColor;
layout (location = 1) in vec2 vertTexCoord;
layout (location = 2) in vec3 vertSkyPos;

layout (location = 0) out vec4 outColor;


#define DESC_SET_TEXTURES 0
#define DESC_SET_GLOBAL_UNIFORM 1
// set 2 is the tonemapping set, carried only so set 3 matches RsSky.frag.
#define DESC_SET_VOLUMETRIC 3
#include "ShaderCommonGLSLFunc.h"
#include "Clouds.h"

layout(push_constant) uniform RasterizerFrag_BT
{
    layout(offset = 64) uint packedColor;
    layout(offset = 68) uint textureIndex;
    layout(offset = 72) uint skyCloudMode;
} rasterizerFragInfo;

layout (constant_id = 0) const uint alphaTest = 0;

#define ALPHA_THRESHOLD 0.5


void main()
{
    vec4 albedoAlpha = getTextureSample(rasterizerFragInfo.textureIndex, vertTexCoord);


    outColor = unpackUintColor( rasterizerFragInfo.packedColor ) * vertColor * albedoAlpha;


    if (alphaTest != 0)
    {
        if (outColor.a < ALPHA_THRESHOLD)
        {
            discard;
        }
    }

    if( cloudsEnabled() && rasterizerFragInfo.skyCloudMode != CLOUD_MODE_NONE )
    {
        vec3 dir = normalize( vertSkyPos );
        outColor = cloudComposite( outColor, dir, rasterizerFragInfo.skyCloudMode );
    }
}
