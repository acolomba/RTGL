/*

Copyright (c) 2026 Doom64-RT contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#pragma once

#include "Common.h"

#include <memory>
#include <string>
#include <vector>

namespace RTGL1
{
class Framebuffers;
struct ShGlobalUniform;

// NVIDIA NRD (ReBLUR / ReLAX / SIGMA) behind NRDIntegration's VK-wrapping
// entry points: the Integration creates an NRI device AROUND RTGL1's existing
// VkDevice (nriCreateDeviceFromVKDevice under the hood) -- RTGL1 keeps owning
// Vulkan, resources are handed over as raw VkImage+VkFormat, command buffers
// as raw VkCommandBuffer. See docs/plan-nrd-denoiser.md in the parent repo;
// the reference is Duke-RT's nri_nrd.cpp.
//
// CURRENT STATE: instance lifecycle only (the liveness probe). EnsureReady
// proves the entire stack on the user's machine -- NRD compiled in, embedded
// SPIR-V shaders unpacked, NRI device wrapped, every pipeline and pool
// created -- which is precisely the part of the port that can fail
// invisibly. The Denoise() data path (pack/demodulate pass, resource
// snapshot, remodulate) is the next stage and lands against this class.
class NrdDenoiser
{
public:
    NrdDenoiser( VkInstance                        instance,
                 VkPhysicalDevice                  physDevice,
                 VkDevice                          device,
                 const std::vector< std::string >& enabledInstanceExtensions,
                 const std::vector< std::string >& enabledDeviceExtensions,
                 uint32_t                          graphicsQueueFamilyIndex,
                 uint32_t                          graphicsQueueNum );
    ~NrdDenoiser();

    NrdDenoiser( const NrdDenoiser& )                = delete;
    NrdDenoiser( NrdDenoiser&& ) noexcept            = delete;
    NrdDenoiser& operator=( const NrdDenoiser& )     = delete;
    NrdDenoiser& operator=( NrdDenoiser&& ) noexcept = delete;

    // (Re)creates the NRD instance at the given render resolution; cheap when
    // already created at that size. Returns false if the stack failed to come
    // up (logged); callers must then fall back to A-SVGF.
    bool EnsureReady( uint32_t renderWidth, uint32_t renderHeight );

    // Stage 2: run ReLAX on the buffers CmNrdPack staged. Records NRD's
    // dispatches (with its own internal barriers) into cmd. Returns false if
    // the instance is not up -- the caller must then fall back to A-SVGF for
    // this frame. gu supplies the column-major non-jittered matrices and the
    // current jitter; the previous jitter is tracked here.
    bool Denoise( VkCommandBuffer         cmd,
                  uint32_t                frameIndex,
                  Framebuffers&           framebuffers,
                  const ShGlobalUniform*  gu,
                  double                  timeDeltaSeconds,
                  bool                    resetHistory,
                  bool                    enableValidation );

    bool   Valid() const;
    double MemoryMb() const;

private:
    void Destroy();

private:
    // NRDIntegration.hpp is a header-only implementation and must live in
    // exactly one translation unit -- everything NRD/NRI-typed hides behind
    // this.
    struct Impl;
    std::unique_ptr< Impl > m_impl;

    VkInstance                 m_instance{ VK_NULL_HANDLE };
    VkPhysicalDevice           m_physDevice{ VK_NULL_HANDLE };
    VkDevice                   m_device{ VK_NULL_HANDLE };
    std::vector< std::string > m_instanceExts;
    std::vector< std::string > m_deviceExts;
    uint32_t                   m_queueFamily{ 0 };
    uint32_t                   m_queueNum{ 1 };

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    bool     m_valid{ false };
    bool     m_failedOnce{ false };

    // NRD CommonSettings state carried across frames
    float    m_jitterPrev[ 2 ]{ 0, 0 };
    uint32_t m_nrdFrameIndex{ 0 };
};

}
