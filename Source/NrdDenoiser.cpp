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

#include "NrdDenoiser.h"

#include "DebugPrint.h"
#include "Framebuffers.h"
#include "Generated/ShaderCommonC.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

#ifdef RG_USE_NRD

// NRDIntegration.hpp is the whole implementation; this must remain the only
// translation unit that includes it. Order matters: NRDIntegration.h checks
// for NRD.h (NRD_VERSION_MAJOR), NRI.h (NRI_VERSION) and Extensions/NRIHelper.h
// (NRI_HELPER_H); including Extensions/NRIWrapperVK.h first also compiles in
// the RecreateVK/DenoiseVK entry points and the TextureVK resource union arm.
#include <NRI.h>
#include <Extensions/NRIHelper.h>
// NRIWrapperVK.h references nri::AccelerationStructureBits, which lives in the
// ray-tracing extension header and is NOT pulled in transitively.
#include <Extensions/NRIRayTracing.h>
#include <Extensions/NRIWrapperVK.h>

#include <NRD.h>
#include <NRDSettings.h>

// NRDIntegration's own step log (NRD-Doom64RT.log in the CWD). It opens the
// file EARLY in Recreate -- before the NRI interfaces are even fetched -- so
// after a hang, file-missing means the block is in nriCreateDeviceFromVKDevice
// itself, and the file's tail otherwise narrows the step. Kept on: the file is
// tiny, and this lane has already hung twice in ways only bracketing found.
#define NRD_INTEGRATION_DEBUG_LOGGING
#include <NRDIntegration.hpp>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    // NRI's Message enum has an ERROR member and its header says "wingdi.h
    // must not be included after" -- NOGDI keeps the ERROR macro out (only
    // loader APIs are used here).
    #ifndef NOGDI
        #define NOGDI
    #endif
    #include <windows.h>
#endif

namespace
{

// Stable identifiers, Duke-RT's set (nri_nrd.cpp): both diffuse+specular
// denoisers are created up front so switching ReBLUR<->ReLAX at runtime is a
// settings change, not an instance rebuild; SIGMA rides along for a future
// shadow lane.
const nrd::DenoiserDesc g_denoisers[] = {
    { nrd::Identifier( 1 ), nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR },
    { nrd::Identifier( 2 ), nrd::Denoiser::RELAX_DIFFUSE_SPECULAR },
    { nrd::Identifier( 3 ), nrd::Denoiser::SIGMA_SHADOW },
};

// NRI's DEFAULT callbacks turn any Message::ERROR into DebugBreak()
// (Creation.cpp AbortExecution) -- inside this game that surfaces as an
// invisible "GZDoom Very Fatal Error" dialog behind the window and reads as
// a 0%-CPU hang (2026-08-17, found by enumerating the process's windows).
// Route messages into RTGL's log instead, and make abort a no-op: NRI's
// creation paths return a failure Result after reporting, which is exactly
// the clean path EnsureReady already handles.
void NRI_CALL NriMessageToLog(
    nri::Message messageType, const char* file, uint32_t line, const char* message, void* )
{
    RTGL1::debug::Warning( "NRI[{}]: {} ({}:{})",
                           messageType == nri::Message::ERROR ? "ERROR"
                           : messageType == nri::Message::WARNING
                               ? "WARNING"
                               : "INFO",
                           message ? message : "(null)",
                           file ? file : "?",
                           line );
}

void NRI_CALL NriAbortToLog( void* )
{
    RTGL1::debug::Warning( "NRI: abort requested after the ERROR above -- continuing so the "
                           "creation path can fail cleanly (NRD lane disabled, A-SVGF "
                           "unaffected)" );
}

// NRI.dll is DELAY-LOADED (CMakeLists /DELAYLOAD:NRI.dll): the app loads
// rt/bin/RTGL1.dll with plain LoadLibraryA, whose DEPENDENCY search covers the
// exe directory and PATH but NOT rt/bin -- a static import of NRI.dll made
// RTGL1.dll itself fail to load, and the loader's error message names
// RTGL1.dll, never the dependency ("rtgl1.dll not found", 2026-08-17). This
// loads NRI.dll by full path from THIS module's own directory before the first
// NRI call; once mapped, the delay-load resolver finds it by base name.
bool PreloadNriFromOwnDirectory()
{
#ifdef _WIN32
    static int s_state = 0; // 0 = untried, 1 = ok, -1 = failed (latched)
    if( s_state != 0 )
    {
        return s_state > 0;
    }

    HMODULE self = nullptr;
    if( !GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast< LPCSTR >( &PreloadNriFromOwnDirectory ),
                             &self ) )
    {
        RTGL1::debug::Warning( "NRD: GetModuleHandleExA failed ({}) -- cannot locate "
                               "RTGL1.dll's own directory to load NRI.dll from",
                               uint32_t( GetLastError() ) );
        s_state = -1;
        return false;
    }

    char path[ MAX_PATH ]{};
    if( GetModuleFileNameA( self, path, MAX_PATH ) == 0 )
    {
        RTGL1::debug::Warning( "NRD: GetModuleFileNameA failed ({})",
                               uint32_t( GetLastError() ) );
        s_state = -1;
        return false;
    }

    if( char* lastSlash = strrchr( path, '\\' ) )
    {
        lastSlash[ 1 ] = '\0';
    }
    strncat_s( path, "NRI.dll", _TRUNCATE );

    if( !LoadLibraryA( path ) )
    {
        RTGL1::debug::Warning( "NRD: LoadLibraryA(\"{}\") failed ({}) -- NRI.dll must sit "
                               "next to RTGL1.dll (tools/build-rtgl.cmd stages it). The NRD "
                               "lane is unavailable this session, A-SVGF unaffected",
                               path,
                               uint32_t( GetLastError() ) );
        s_state = -1;
        return false;
    }

    s_state = 1;
    return true;
#else
    return true;
#endif
}

}

struct RTGL1::NrdDenoiser::Impl
{
    nrd::Integration integration{};
};

RTGL1::NrdDenoiser::NrdDenoiser( VkInstance                        instance,
                                 VkPhysicalDevice                  physDevice,
                                 VkDevice                          device,
                                 const std::vector< std::string >& enabledInstanceExtensions,
                                 const std::vector< std::string >& enabledDeviceExtensions,
                                 uint32_t                          graphicsQueueFamilyIndex,
                                 uint32_t                          graphicsQueueNum )
    : m_impl{ std::make_unique< Impl >() }
    , m_instance{ instance }
    , m_physDevice{ physDevice }
    , m_device{ device }
    , m_instanceExts{ enabledInstanceExtensions }
    , m_deviceExts{ enabledDeviceExtensions }
    , m_queueFamily{ graphicsQueueFamilyIndex }
    , m_queueNum{ graphicsQueueNum }
{
}

RTGL1::NrdDenoiser::~NrdDenoiser()
{
    Destroy();
}

void RTGL1::NrdDenoiser::Destroy()
{
    if( m_valid && m_impl )
    {
        // autoWaitForIdle is OFF (see EnsureReady -- a mid-frame device wait
        // deadlocks this engine), so destroying with NRD work in flight is on
        // the caller. Stage 1 records no NRD GPU work at all, and the real
        // teardown paths (device shutdown, resolution change) already sit
        // behind RTGL's own wait-idle points.
        m_impl->integration.Destroy();
    }
    m_valid  = false;
    m_width  = 0;
    m_height = 0;
}

bool RTGL1::NrdDenoiser::EnsureReady( uint32_t renderWidth, uint32_t renderHeight )
{
    if( m_valid && m_width == renderWidth && m_height == renderHeight )
    {
        return true;
    }
    if( m_failedOnce )
    {
        // A failed stack will not heal by retrying every frame; it would only
        // spam vkDeviceWaitIdle. One shot per session, loudly logged.
        return false;
    }

    // Before ANY call that crosses into NRI: the delay-load resolver must be
    // able to find NRI.dll, or the first nri* call raises the delay-load SEH
    // exception instead of failing cleanly.
    if( !PreloadNriFromOwnDirectory() )
    {
        m_failedOnce = true;
        return false;
    }

    // Bracket logs: the first bring-up blocked the render thread with zero
    // CPU (2026-08-17) and the only way to find WHERE without a debugger is
    // to print before the step that never returns.
    debug::Warning( "NRD: NRI.dll loaded; creating instance at {}x{}...",
                    renderWidth,
                    renderHeight );

    Destroy();

    auto integrationDesc = nrd::IntegrationCreationDesc{};
    snprintf( integrationDesc.name, sizeof( integrationDesc.name ), "Doom64RT" );
    integrationDesc.resourceWidth  = uint16_t( renderWidth );
    integrationDesc.resourceHeight = uint16_t( renderHeight );
    integrationDesc.queuedFrameNum = uint8_t( MAX_FRAMES_IN_FLIGHT );
    // MUST be false. autoWaitForIdle makes RecreatePipelines/Destroy call
    // DeviceWaitIdle, and EnsureReady runs MID-FRAME on the render thread --
    // in this engine the DXGI-present interop keeps cross-API timeline waits
    // pending on the queue, so a mid-frame device-wait-idle never returns:
    // render thread parked at 0% CPU, window still pumping (2026-08-17, found
    // by bracketing). On first create there is nothing NRD-related in flight
    // anyway; resize/teardown ordering is on US -- Duke-RT runs 'false' too
    // and manages its own idles.
    integrationDesc.autoWaitForIdle = false;

    auto instanceCreationDesc         = nrd::InstanceCreationDesc{};
    instanceCreationDesc.denoisers    = g_denoisers;
    instanceCreationDesc.denoisersNum = uint32_t( std::size( g_denoisers ) );

    // NRI wraps RTGL1's existing Vulkan objects; it does not own them. The
    // lists tell NRI which capabilities it may use -- and it eagerly resolves
    // dispatch tables for every extension it is told about, treating a
    // missing entry point as FATAL. Declaring RTGL's full list made it demand
    // vkCmdTraceRaysIndirect2KHR (ray_tracing_maintenance1, which RTGL never
    // enables) just because ray_tracing_pipeline was mentioned (2026-08-17).
    // This wrapped device exists ONLY to run NRD's compute dispatches, so
    // declare exactly the intersection it needs: the three hard requirements
    // plus the memory-budget query. Device extensions are filtered against
    // what the device was ACTUALLY created with, so a lying declaration is
    // impossible.
    auto instExts = std::vector< const char* >{};
    for( const auto& e : m_instanceExts )
    {
        instExts.push_back( e.c_str() );
    }
    static constexpr const char* NRI_RELEVANT_DEVICE_EXTS[] = {
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
        VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
    };
    auto devExts = std::vector< const char* >{};
    for( const auto& e : m_deviceExts )
    {
        for( const char* allowed : NRI_RELEVANT_DEVICE_EXTS )
        {
            if( e == allowed )
            {
                devExts.push_back( e.c_str() );
                break;
            }
        }
    }

    // MUST match the register shifts NRD's SPIR-V was generated with
    // (tools/build-nrd-deps.cmd: --sRegShift 0 --bRegShift 2 --uRegShift 3
    // --tRegShift 20) or every descriptor lands on the wrong binding.
    // Struct order is s, t, b, u -- NOT the shift-flag order.
    const auto bindingOffsets = nri::VKBindingOffsets{
        .sRegister = 0,
        .tRegister = 20,
        .bRegister = 2,
        .uRegister = 3,
    };

    const auto queueFamily = nri::QueueFamilyVKDesc{
        .queueNum    = m_queueNum,
        .queueType   = nri::QueueType::GRAPHICS,
        .familyIndex = m_queueFamily,
    };

    auto deviceDesc              = nri::DeviceCreationVKDesc{};
    deviceDesc.callbackInterface = nri::CallbackInterface{
        .MessageCallback = NriMessageToLog,
        .AbortExecution  = NriAbortToLog,
        .userArg         = nullptr,
    };
    deviceDesc.vkBindingOffsets = bindingOffsets;
    deviceDesc.vkExtensions     = nri::VKExtensions{
            .instanceExtensions   = instExts.data(),
            .instanceExtensionNum = uint32_t( instExts.size() ),
            .deviceExtensions     = devExts.data(),
            .deviceExtensionNum   = uint32_t( devExts.size() ),
    };
    deviceDesc.vkInstance       = m_instance;
    deviceDesc.vkDevice         = m_device;
    deviceDesc.vkPhysicalDevice = m_physDevice;
    deviceDesc.queueFamilies    = &queueFamily;
    deviceDesc.queueFamilyNum   = 1;
    deviceDesc.minorVersion     = 2; // VulkanDevice_Init.cpp: VK_API_VERSION_1_2

    const nrd::Result r =
        m_impl->integration.RecreateVK( integrationDesc, instanceCreationDesc, deviceDesc );

    if( r != nrd::Result::SUCCESS )
    {
        // WARNING severity so it is visible without -rtdebug -- a denoiser
        // lane that silently fails to come up is this project's oldest bug
        // shape.
        debug::Warning( "NRD: NRDIntegration RecreateVK FAILED ({}) at {}x{} -- "
                        "the NRD lane is unavailable this session, A-SVGF unaffected",
                        int( r ),
                        renderWidth,
                        renderHeight );
        m_failedOnce = true;
        return false;
    }

    m_valid  = true;
    m_width  = renderWidth;
    m_height = renderHeight;

    debug::Warning( "NRD: instance ALIVE at {}x{} -- ReBLUR+ReLAX+SIGMA created, embedded "
                    "SPIR-V loaded, NRI wrapped the existing VkDevice; pools {:.1f} MB "
                    "(persistent {:.1f} + aliasable {:.1f}). Stage 2 active: ReLAX is the "
                    "denoiser this frame onward (pack -> DenoiseVK -> compose).",
                    renderWidth,
                    renderHeight,
                    m_impl->integration.GetTotalMemoryUsageInMb(),
                    m_impl->integration.GetPersistentMemoryUsageInMb(),
                    m_impl->integration.GetAliasableMemoryUsageInMb() );
    return true;
}

bool RTGL1::NrdDenoiser::Denoise( VkCommandBuffer        cmd,
                                  uint32_t               frameIndex,
                                  Framebuffers&          framebuffers,
                                  const ShGlobalUniform* gu,
                                  double                 timeDeltaSeconds,
                                  bool                   resetHistory,
                                  bool                   enableValidation,
                                  const Tuning&          tuning )
{
    if( !m_valid || !gu )
    {
        return false;
    }

    m_impl->integration.NewFrame();

    // ReLAX defaults + the live tuning block (0 = keep the default). Anti-
    // firefly on by default because this game's 1-spp ReSTIR genuinely
    // produces fireflies (A-SVGF ships a pass for them).
    {
        auto relax              = nrd::RelaxSettings{};
        relax.enableAntiFirefly = tuning.antiFirefly;

        if( tuning.maxAccumFrames != 0 )
        {
            relax.diffuseMaxAccumulatedFrameNum  = tuning.maxAccumFrames;
            relax.specularMaxAccumulatedFrameNum = tuning.maxAccumFrames;
        }
        if( tuning.fastAccumFrames != 0 )
        {
            relax.diffuseMaxFastAccumulatedFrameNum  = tuning.fastAccumFrames;
            relax.specularMaxFastAccumulatedFrameNum = tuning.fastAccumFrames;
        }
        if( tuning.atrousIterations != 0 )
        {
            relax.atrousIterationNum = std::clamp( tuning.atrousIterations, 2u, 8u );
        }
        if( tuning.prepassDiffuse > 0.0f )
        {
            relax.diffusePrepassBlurRadius = tuning.prepassDiffuse;
        }
        if( tuning.prepassSpecular > 0.0f )
        {
            relax.specularPrepassBlurRadius = tuning.prepassSpecular;
        }
        if( tuning.phiLuminance > 0.0f )
        {
            relax.diffusePhiLuminance = tuning.phiLuminance;
        }
        if( tuning.minHitDistWeight > 0.0f )
        {
            relax.minHitDistanceWeight = std::min( tuning.minHitDistWeight, 0.2f );
        }

        // Read the arm back: the settings that actually reached NRD, edge-
        // triggered on change (the project's oldest rule).
        {
            static bool     s_have = false;
            static uint32_t s_prev[ 4 ]  = {};
            static float    s_prevF[ 4 ] = {};
            const uint32_t  curU[ 4 ]  = { relax.diffuseMaxAccumulatedFrameNum,
                                           relax.diffuseMaxFastAccumulatedFrameNum,
                                           relax.atrousIterationNum,
                                           uint32_t( relax.enableAntiFirefly ) };
            const float     curF[ 4 ]  = { relax.diffusePrepassBlurRadius,
                                           relax.specularPrepassBlurRadius,
                                           relax.diffusePhiLuminance,
                                           relax.minHitDistanceWeight };
            if( !s_have || memcmp( s_prev, curU, sizeof( curU ) ) != 0 ||
                memcmp( s_prevF, curF, sizeof( curF ) ) != 0 )
            {
                s_have = true;
                memcpy( s_prev, curU, sizeof( curU ) );
                memcpy( s_prevF, curF, sizeof( curF ) );
                debug::Warning( "NRD/ReLAX settings: accum={} fast={} atrous={} "
                                "prepassD={:.0f} prepassS={:.0f} phiLum={:.1f} "
                                "minHitDistW={:.2f} antiFirefly={}",
                                curU[ 0 ],
                                curU[ 1 ],
                                curU[ 2 ],
                                curF[ 0 ],
                                curF[ 1 ],
                                curF[ 2 ],
                                curF[ 3 ],
                                curU[ 3 ] ? "on" : "off" );
            }
        }

        if( m_impl->integration.SetDenoiserSettings( nrd::Identifier( 2 ), &relax ) !=
            nrd::Result::SUCCESS )
        {
            debug::Warning( "NRD: SetDenoiserSettings(RELAX) failed" );
            return false;
        }
    }

    auto common = nrd::CommonSettings{};
    // Column-major, vector-is-column, NON-jittered -- exactly what
    // gu->view/projection are (RTGL adds jitter in raygen / at raster time,
    // never into the matrices).
    static_assert( sizeof( common.viewToClipMatrix ) == sizeof( gu->projection ) );
    memcpy( common.viewToClipMatrix, gu->projection, sizeof( common.viewToClipMatrix ) );
    memcpy( common.viewToClipMatrixPrev, gu->projectionPrev, sizeof( common.viewToClipMatrixPrev ) );
    memcpy( common.worldToViewMatrix, gu->view, sizeof( common.worldToViewMatrix ) );
    memcpy( common.worldToViewMatrixPrev, gu->viewPrev, sizeof( common.worldToViewMatrixPrev ) );

    // CmNrdPack writes motion as our native 2.5D contract: xy = UV delta
    // cur->prev, z = distance delta -- so no conversion scale.
    common.motionVectorScale[ 0 ] = 1.0f;
    common.motionVectorScale[ 1 ] = 1.0f;
    common.motionVectorScale[ 2 ] = 1.0f;
    common.isMotionVectorInWorldSpace = false;

    common.cameraJitter[ 0 ]     = gu->jitterX;
    common.cameraJitter[ 1 ]     = gu->jitterY;
    common.cameraJitterPrev[ 0 ] = m_jitterPrev[ 0 ];
    common.cameraJitterPrev[ 1 ] = m_jitterPrev[ 1 ];
    m_jitterPrev[ 0 ]            = gu->jitterX;
    m_jitterPrev[ 1 ]            = gu->jitterY;

    common.resourceSize[ 0 ]     = uint16_t( m_width );
    common.resourceSize[ 1 ]     = uint16_t( m_height );
    common.resourceSizePrev[ 0 ] = uint16_t( m_width );
    common.resourceSizePrev[ 1 ] = uint16_t( m_height );
    common.rectSize[ 0 ]         = uint16_t( m_width );
    common.rectSize[ 1 ]         = uint16_t( m_height );
    common.rectSizePrev[ 0 ]     = uint16_t( m_width );
    common.rectSizePrev[ 1 ]     = uint16_t( m_height );

    common.viewZScale             = 1.0f;
    common.denoisingRange         = 100000.0f;
    common.disocclusionThreshold  = 0.01f;
    common.frameIndex             = m_nrdFrameIndex++;
    common.timeDeltaBetweenFrames = float( timeDeltaSeconds * 1000.0 );
    common.accumulationMode =
        resetHistory ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;
    // ReLAX uses base-color/metalness to patch specular motion (Duke-RT sets
    // this true for RELAX and false for REBLUR).
    common.isBaseColorMetalnessAvailable = true;
    common.enableValidation              = enableValidation;

    if( m_impl->integration.SetCommonSettings( common ) != nrd::Result::SUCCESS )
    {
        debug::Warning( "NRD: SetCommonSettings failed" );
        return false;
    }

    // Raw VkImage+format handover; the declared state is the compute/storage
    // state everything RTGL-side reads and writes these images in, and
    // restoreInitialState returns them to it, so RTGL's "everything stays
    // GENERAL" assumption is never violated outside the Denoise call.
    auto makeRes = [ & ]( FramebufferImageIndex fi ) {
        auto [ image, view, format ] = framebuffers.GetImageHandles( fi, frameIndex );

        nrd::Resource r  = {};
        r.vk.image       = ( VKNonDispatchableHandle )( image );
        r.vk.format      = VKEnum( format );
        r.state          = nri::AccessLayoutStage{ nri::AccessBits::SHADER_RESOURCE_STORAGE,
                                                   nri::Layout::SHADER_RESOURCE_STORAGE,
                                                   nri::StageBits::COMPUTE_SHADER };
        return r;
    };

    auto snapshot                = nrd::ResourceSnapshot{};
    snapshot.restoreInitialState = true;
    snapshot.SetResource( nrd::ResourceType::IN_MV, makeRes( FB_IMAGE_INDEX_NRD_MOTION ) );
    snapshot.SetResource( nrd::ResourceType::IN_VIEWZ, makeRes( FB_IMAGE_INDEX_NRD_VIEW_Z ) );
    snapshot.SetResource( nrd::ResourceType::IN_NORMAL_ROUGHNESS,
                          makeRes( FB_IMAGE_INDEX_NRD_NORMAL_ROUGHNESS ) );
    snapshot.SetResource( nrd::ResourceType::IN_BASECOLOR_METALNESS,
                          makeRes( FB_IMAGE_INDEX_NRD_BASE_COLOR_METALNESS ) );
    snapshot.SetResource( nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST,
                          makeRes( FB_IMAGE_INDEX_NRD_DIFFUSE ) );
    snapshot.SetResource( nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST,
                          makeRes( FB_IMAGE_INDEX_NRD_SPECULAR ) );
    snapshot.SetResource( nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST,
                          makeRes( FB_IMAGE_INDEX_NRD_DIFFUSE_OUT ) );
    snapshot.SetResource( nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST,
                          makeRes( FB_IMAGE_INDEX_NRD_SPECULAR_OUT ) );
    snapshot.SetResource( nrd::ResourceType::OUT_VALIDATION,
                          makeRes( FB_IMAGE_INDEX_NRD_VALIDATION ) );

    const nrd::Identifier relaxId = nrd::Identifier( 2 );
    const auto            cmdDesc = nri::CommandBufferVKDesc{
                   .vkCommandBuffer = cmd,
                   .queueType       = nri::QueueType::GRAPHICS,
    };
    m_impl->integration.DenoiseVK( &relaxId, 1, cmdDesc, snapshot );

    return true;
}

bool RTGL1::NrdDenoiser::Valid() const
{
    return m_valid;
}

double RTGL1::NrdDenoiser::MemoryMb() const
{
    return m_valid ? m_impl->integration.GetTotalMemoryUsageInMb() : 0.0;
}

#else // !RG_USE_NRD

struct RTGL1::NrdDenoiser::Impl
{
};

RTGL1::NrdDenoiser::NrdDenoiser( VkInstance                        instance,
                                 VkPhysicalDevice                  physDevice,
                                 VkDevice                          device,
                                 const std::vector< std::string >& enabledInstanceExtensions,
                                 const std::vector< std::string >& enabledDeviceExtensions,
                                 uint32_t                          graphicsQueueFamilyIndex,
                                 uint32_t                          graphicsQueueNum )
    : m_instance{ instance }
    , m_physDevice{ physDevice }
    , m_device{ device }
    , m_instanceExts{ enabledInstanceExtensions }
    , m_deviceExts{ enabledDeviceExtensions }
    , m_queueFamily{ graphicsQueueFamilyIndex }
    , m_queueNum{ graphicsQueueNum }
{
    debug::Warning( "NRD: compiled OUT of this RTGL1 build (RG_USE_NRD off) -- "
                    "rt_nrd has no effect" );
}

RTGL1::NrdDenoiser::~NrdDenoiser() = default;

void RTGL1::NrdDenoiser::Destroy() {}

bool RTGL1::NrdDenoiser::EnsureReady( uint32_t, uint32_t )
{
    return false;
}

bool RTGL1::NrdDenoiser::Denoise( VkCommandBuffer,
                                  uint32_t,
                                  Framebuffers&,
                                  const ShGlobalUniform*,
                                  double,
                                  bool,
                                  bool,
                                  const Tuning& )
{
    return false;
}

bool RTGL1::NrdDenoiser::Valid() const
{
    return false;
}

double RTGL1::NrdDenoiser::MemoryMb() const
{
    return 0.0;
}

#endif // RG_USE_NRD
