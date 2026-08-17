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

#include <cstdio>
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

#include <NRDIntegration.hpp>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
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
        // autoWaitForIdle is enabled on creation, so Destroy is safe with
        // work in flight.
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

    Destroy();

    auto integrationDesc = nrd::IntegrationCreationDesc{};
    snprintf( integrationDesc.name, sizeof( integrationDesc.name ), "Doom64RT" );
    integrationDesc.resourceWidth  = uint16_t( renderWidth );
    integrationDesc.resourceHeight = uint16_t( renderHeight );
    integrationDesc.queuedFrameNum = uint8_t( MAX_FRAMES_IN_FLIGHT );
    integrationDesc.autoWaitForIdle = true;

    auto instanceCreationDesc         = nrd::InstanceCreationDesc{};
    instanceCreationDesc.denoisers    = g_denoisers;
    instanceCreationDesc.denoisersNum = uint32_t( std::size( g_denoisers ) );

    // NRI wraps RTGL1's existing Vulkan objects; it does not own them. The
    // extension lists are the ones the instance/device were actually created
    // with (VulkanDevice_Init retains them for exactly this call) -- NRI
    // gates its own capability usage on what it is told is enabled.
    auto instExts = std::vector< const char* >{};
    for( const auto& e : m_instanceExts )
    {
        instExts.push_back( e.c_str() );
    }
    auto devExts = std::vector< const char* >{};
    for( const auto& e : m_deviceExts )
    {
        devExts.push_back( e.c_str() );
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

    auto deviceDesc             = nri::DeviceCreationVKDesc{};
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
                    "(persistent {:.1f} + aliasable {:.1f}). PROBE ONLY: the Denoise() data "
                    "path is not wired yet, A-SVGF is still the active denoiser.",
                    renderWidth,
                    renderHeight,
                    m_impl->integration.GetTotalMemoryUsageInMb(),
                    m_impl->integration.GetPersistentMemoryUsageInMb(),
                    m_impl->integration.GetAliasableMemoryUsageInMb() );
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

bool RTGL1::NrdDenoiser::Valid() const
{
    return false;
}

double RTGL1::NrdDenoiser::MemoryMb() const
{
    return 0.0;
}

#endif // RG_USE_NRD
