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

#pragma once

#include "BlueNoise.h"
#include "CommandBufferManager.h"
#include "Framebuffers.h"
#include "GlobalUniform.h"
#include "IShaderDependency.h"
#include "MemoryAllocator.h"

// Doom64-RT: matches ILLUMINATION_VOLUME in Generated/GenerateShaderCommon.py (the
// static_assert in Volumetric.cpp enforces they stay in sync). Turned on so rasterized
// translucent sprites (spectres, nightmare imps) can sample real room irradiance instead
// of always rendering at full brightness -- see RsWorld.inl's illumVolumeEnable branch.
#define ILLUMINATION_VOLUME_ 1

namespace RTGL1
{
class Volumetric : public IShaderDependency
{
public:
    Volumetric( VkDevice              device,
                CommandBufferManager& cmdManager,
                MemoryAllocator&      allocator,
                const ShaderManager&  shaderManager,
                const GlobalUniform&  uniform,
                const BlueNoise&      rnd,
                const Framebuffers&   framebuffers );
    ~Volumetric() override;

    Volumetric( const Volumetric& other )                = delete;
    Volumetric( Volumetric&& other ) noexcept            = delete;
    Volumetric& operator=( const Volumetric& other )     = delete;
    Volumetric& operator=( Volumetric&& other ) noexcept = delete;

    VkDescriptorSetLayout GetDescSetLayout() const;
    VkDescriptorSet       GetDescSet( uint32_t frameIndex ) const;

    void ProcessScattering( VkCommandBuffer      cmd,
                            uint32_t             frameIndex,
                            const GlobalUniform& uniform,
                            const BlueNoise&     rnd,
                            const Framebuffers&  framebuffers,
                            float                maxHistoryLength );
    void BarrierToReadIllumination( VkCommandBuffer cmd, uint32_t frameIndex );

    // Doom64-RT: march the volumetric cloud slab into this frame's cloud map
    // (CmCloudMap.comp) and leave it readable by fragment shaders. Call before
    // the sky is rasterised. When the clouds are disabled in the uniform the
    // map is still cleared to "no cloud" once, so a stale map cannot show.
    void ProcessClouds( VkCommandBuffer      cmd,
                        uint32_t             frameIndex,
                        const GlobalUniform& uniform,
                        const BlueNoise&     rnd );

    void OnShaderReload( const ShaderManager* shaderManager ) override;

private:
    void CreateSampler();
    void CreateImages( CommandBufferManager& cmdManager, MemoryAllocator& allocator );

    void CreateDescriptors();
    void UpdateDescriptors();

    void CreatePipelineLayouts( const GlobalUniform& uniform,
                                const BlueNoise&     rnd,
                                const Framebuffers&  framebuffers );
    void CreatePipelines( const ShaderManager& shaderManager );
    void DestroyPipelines();

private:
    VkDevice device{ VK_NULL_HANDLE };

    struct VolumeDef
    {
        VkImage        image{ VK_NULL_HANDLE };
        VkImageView    view{ VK_NULL_HANDLE };
        VkDeviceMemory memory{ VK_NULL_HANDLE };
    };

    VolumeDef scattering[ MAX_FRAMES_IN_FLIGHT ]{};
#if ILLUMINATION_VOLUME_
    // DOUBLE BUFFERED, like scattering above, and for a reason that bit hard:
    // RtVolumetric.rgen both writes this volume and reads last frame's value out
    // of it. With a single image those are the same memory, so the read is only
    // safe at the SAME cell index -- which is why the temporal blend was
    // unreprojected, and therefore invalid the moment the camera moved, since
    // the grid is camera-attached. Two images let the shader read the previous
    // frame properly and reproject into it.
    VolumeDef illumination[ MAX_FRAMES_IN_FLIGHT ]{};
#endif
    // Doom64-RT: the cloud map, a 2D lat-long image of world directions.
    // Double-buffered for the same reason as the two above: the march blends
    // in last frame's map at the same texel.
    VolumeDef cloudMap[ MAX_FRAMES_IN_FLIGHT ]{};
    // Frames the cloud map has been cleared for while disabled; the clear is
    // repeated for each in-flight image and then skipped.
    uint32_t cloudMapClearedMask{ 0 };

    VkSampler volumeSampler{ VK_NULL_HANDLE };
    // Wraps in u (azimuth), clamps in v (altitude).
    VkSampler cloudSampler{ VK_NULL_HANDLE };

    VkDescriptorPool      descPool{ VK_NULL_HANDLE };
    VkDescriptorSetLayout descLayout{ VK_NULL_HANDLE };
    VkDescriptorSet       descSets[ MAX_FRAMES_IN_FLIGHT ]{};

    VkPipelineLayout processPipelineLayout{ VK_NULL_HANDLE };
    VkPipeline       processPipeline{ VK_NULL_HANDLE };

    VkPipelineLayout accumPipelineLayout{ VK_NULL_HANDLE };
    VkPipeline       accumPipeline{ VK_NULL_HANDLE };

    // Shares processPipelineLayout (volumetric set, uniform, blue noise).
    VkPipeline cloudPipeline{ VK_NULL_HANDLE };
};
}
