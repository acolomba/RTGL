// Copyright (c) 2021 Sultim Tsyrendashiev
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

// The sky, drawn at RENDER RESOLUTION into the albedo framebuffer. This is
// what the player looks at: RaygenPrimary.inl does not sample the cubemap for
// camera rays when the sky is rasterized geometry, it reuses this pass.
//
// Doom64-RT: composites the volumetric cloud map (Clouds.h). The world
// direction of the fragment is the camera ray through it -- the sky is drawn
// with the view's translation removed, so the direction from the camera
// through the pixel IS the direction to the sky point, and the vertex stage
// needs no change.

#version 460

layout (location = 0) in vec4 vertColor;
layout (location = 1) in vec2 vertTexCoord;

layout (location = 0) out vec4 outColor;


#define DESC_SET_TEXTURES 0
#define DESC_SET_GLOBAL_UNIFORM 1
// set 2 is the tonemapping set in the raster pass layout; not needed here.
#define DESC_SET_VOLUMETRIC 3
#include "ShaderCommonGLSLFunc.h"
#include "Clouds.h"

layout(push_constant) uniform RasterizerFrag_BT
{
    layout(offset = 64) uint packedColor;
    layout(offset = 68) uint textureIndex;
    layout(offset = 72) uint emissiveTextureIndex;
    layout(offset = 76) uint emissiveMult;
    layout(offset = 80) uint normalTextureIndex;
    layout(offset = 84) uint manualSrgb;
    layout(offset = 88) uint skyCloudMode;
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
        vec2 uv  = gl_FragCoord.xy / vec2( globalUniform.renderWidth, globalUniform.renderHeight );
        vec3 dir = getRayDir( uv );
        outColor = cloudComposite( outColor, dir, rasterizerFragInfo.skyCloudMode );
    }
}
