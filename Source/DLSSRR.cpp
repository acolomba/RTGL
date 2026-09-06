/*

Copyright (c) 2026 Doom64-RT contributors
Copyright (c) 2024 V.Shirokii
Copyright (c) 2021 Sultim Tsyrendashiev

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

#include "DLSSRR.h"

#ifdef RG_USE_NATIVE_DLSS2

#include "RTGL1/RTGL1.h"

#include "CmdLabel.h"
#include "DLSS2.h"
#include "LibraryConfig.h"
#include "RenderResolutionHelper.h"
#include "Utils.h"

#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_helpers_dlssd.h>
#include <nvsdk_ngx_helpers_dlssd_vk.h>

#include <cstring>
#include <filesystem>

namespace
{

NVSDK_NGX_PerfQuality_Value ToNGXPerfQuality( RgRenderResolutionMode mode )
{
    switch( mode )
    {
        case RG_RENDER_RESOLUTION_MODE_ULTRA_PERFORMANCE:
            return NVSDK_NGX_PerfQuality_Value::NVSDK_NGX_PerfQuality_Value_UltraPerformance;
        case RG_RENDER_RESOLUTION_MODE_PERFORMANCE:
            return NVSDK_NGX_PerfQuality_Value::NVSDK_NGX_PerfQuality_Value_MaxPerf;
        case RG_RENDER_RESOLUTION_MODE_BALANCED:
            return NVSDK_NGX_PerfQuality_Value::NVSDK_NGX_PerfQuality_Value_Balanced;
        case RG_RENDER_RESOLUTION_MODE_QUALITY:
            return NVSDK_NGX_PerfQuality_Value::NVSDK_NGX_PerfQuality_Value_MaxQuality;
        case RG_RENDER_RESOLUTION_MODE_NATIVE_AA:
            return NVSDK_NGX_PerfQuality_Value::NVSDK_NGX_PerfQuality_Value_DLAA;
        default:
            assert( 0 );
            return NVSDK_NGX_PerfQuality_Value::NVSDK_NGX_PerfQuality_Value_Balanced;
    }
}

} // namespace


RTGL1::DLSSRR::DLSSRR( VkDevice device, bool ngxAlreadyInitialized ) : m_device{ device }
{
    if( !ngxAlreadyInitialized )
    {
        debug::Warning( "DLSSRR: NGX was not initialized by DLSS2; Ray Reconstruction disabled" );
        return;
    }

#ifdef _WIN32
    const auto binFolder = Utils::FindBinFolder();
    if( !exists( binFolder / "nvngx_dlssd.dll" ) )
    {
        debug::Warning( "DLSSRR: Disabled, as DLL file was not found: {}",
                        ( binFolder / "nvngx_dlssd.dll" ).string() );
        return;
    }
#else
    // Linux ships the Ray Reconstruction runtime as libnvidia-ngx-dlssd.so.*
    // next to the renderer, found the same way DLSS2's runtime is; without it
    // NGX fails at feature creation with nothing in the log.
    {
        const auto      moduleDir = Utils::GetModuleDirectory();
        bool            found     = false;
        std::error_code ec;
        for( const auto& entry : std::filesystem::directory_iterator{ moduleDir, ec } )
        {
            const auto name = entry.path().filename().string();
            if( name.find( "nvngx_dlssd" ) != std::string::npos ||
                name.find( "nvidia-ngx-dlssd" ) != std::string::npos )
            {
                found = true;
                break;
            }
        }
        if( !found )
        {
            debug::Warning( "DLSSRR: Disabled, as the Ray Reconstruction runtime (nvngx_dlssd / "
                            "libnvidia-ngx-dlssd) was not found in {}",
                            moduleDir.string() );
            return;
        }
    }
#endif

    NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_GetCapabilityParameters( &m_params );
    if( NVSDK_NGX_FAILED( r ) || !m_params )
    {
        debug::Error( "DLSSRR: NVSDK_NGX_VULKAN_GetCapabilityParameters fail: {}",
                      static_cast< int >( r ) );
        Destroy();
        return;
    }

    {
        int needsUpdatedDriver = 0;
        NVSDK_NGX_Result r_upd = m_params->Get(
            NVSDK_NGX_Parameter_SuperSamplingDenoising_NeedsUpdatedDriver, &needsUpdatedDriver );
        if( NVSDK_NGX_SUCCEED( r_upd ) && needsUpdatedDriver )
        {
            debug::Error( "DLSSRR: Can't load: Outdated driver for Ray Reconstruction" );
            Destroy();
            return;
        }
    }
    {
        int isSupported = 0;
        r = m_params->Get( NVSDK_NGX_Parameter_SuperSamplingDenoising_Available, &isSupported );
        if( NVSDK_NGX_FAILED( r ) || !isSupported )
        {
            NVSDK_NGX_Result featureInitResult{};
            NVSDK_NGX_Parameter_GetI(
                m_params,
                NVSDK_NGX_Parameter_SuperSamplingDenoising_FeatureInitResult,
                reinterpret_cast< int* >( &featureInitResult ) );
            debug::Error( "DLSSRR: Not available. FeatureInitResult={}",
                          static_cast< int >( featureInitResult ) );
            Destroy();
            return;
        }
    }

    m_initialized = true;
    debug::Info( "DLSSRR: Ray Reconstruction available" );
}


bool RTGL1::DLSSRR::Valid() const
{
    return m_initialized && m_params;
}

RTGL1::DLSSRR::~DLSSRR()
{
    Destroy();
}


void RTGL1::DLSSRR::Destroy()
{
    // Do NOT call NVSDK_NGX_VULKAN_Shutdown — DLSS2 owns NGX lifetime.
    if( m_device )
    {
        vkDeviceWaitIdle( m_device );
    }

    if( m_feature )
    {
        NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_ReleaseFeature( m_feature );
        assert( NVSDK_NGX_SUCCEED( r ) );
        m_feature = nullptr;
    }

    if( m_params )
    {
        NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_DestroyParameters( m_params );
        assert( NVSDK_NGX_SUCCEED( r ) );
        m_params = nullptr;
    }

    m_initialized = false;
}

namespace RTGL1
{
namespace
{
    auto CreateDlssdFeature( NVSDK_NGX_Parameter*   params,
                             VkDevice               device,
                             VkCommandBuffer        cmd,
                             const ResolutionState& resolution,
                             RgRenderResolutionMode mode,
                             uint32_t               presetFromCaller,
                             NVSDK_NGX_Handle*      oldFeature ) -> NVSDK_NGX_Handle*
    {
        constexpr unsigned int creationNodeMask   = 1;
        constexpr unsigned int visibilityNodeMask = 1;

        if( oldFeature != nullptr )
        {
            vkDeviceWaitIdle( device );

            NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_ReleaseFeature( oldFeature );
            if( NVSDK_NGX_FAILED( r ) )
            {
                debug::Warning( "DLSSRR: NVSDK_NGX_VULKAN_ReleaseFeature fail: {}",
                                static_cast< int >( r ) );
            }
        }

        auto dlssdParams = NVSDK_NGX_DLSSD_Create_Params{
            .InDenoiseMode          = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified,
            .InRoughnessMode        = NVSDK_NGX_DLSS_Roughness_Mode_Packed,
            .InUseHWDepth           = NVSDK_NGX_DLSS_Depth_Type_HW,
            .InWidth                = resolution.renderWidth,
            .InHeight               = resolution.renderHeight,
            .InTargetWidth          = resolution.upscaledWidth,
            .InTargetHeight         = resolution.upscaledHeight,
            .InPerfQualityValue     = ToNGXPerfQuality( mode ),
            .InFeatureCreateFlags   = 0,
            .InEnableOutputSubrects = false,
        };

        dlssdParams.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        dlssdParams.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;

        // Only Default / D / E are meaningful: A/B/C were removed in SDK
        // 310.4.0, and F..O silently revert to default behaviour
        // (nvsdk_ngx_defs_dlssd.h:38-53).
        //   0 = Default (the DLL picks; NVIDIA's current recommendation)
        //   D = default transformer model
        //   E = latest transformer model (required only if a DoF guide is used;
        //       we pass none)
        // The value is pinned across all five quality slots so the preset does
        // not change under the user when rt_upscale_dlss switches mode --
        // otherwise an A/B of image quality would silently also be an A/B of
        // preset.
        //
        // Was hard-coded E ("preset D was A/B'd in-game on 2026-08-07 and was
        // clearly worse"); now passed through from rt_rr_preset, whose default
        // keeps E. Changing it re-creates the feature (the caller keys on the
        // preset exactly as it keys on a resize), so it takes effect live.
        const uint32_t RR_PRESET = presetFromCaller;

        for( const char* slot : {
                 NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_DLAA,
                 NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality,
                 NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced,
                 NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance,
                 NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance,
             } )
        {
            NVSDK_NGX_Parameter_SetUI( params, slot, RR_PRESET );
        }

        debug::Warning( "DLSSRR: using Ray Reconstruction preset {} (raw value {})",
                        RR_PRESET == NVSDK_NGX_RayReconstruction_Hint_Render_Preset_D ? "D"
                        : RR_PRESET == NVSDK_NGX_RayReconstruction_Hint_Render_Preset_E
                            ? "E"
                            : "Default/other",
                        RR_PRESET );

        NVSDK_NGX_Handle* newFeature{ nullptr };
        NVSDK_NGX_Result  r = NGX_VULKAN_CREATE_DLSSD_EXT1( device,
                                                           cmd,
                                                           creationNodeMask,
                                                           visibilityNodeMask,
                                                           &newFeature,
                                                           params,
                                                           &dlssdParams );
        if( NVSDK_NGX_FAILED( r ) )
        {
            debug::Warning( "DLSSRR: NGX_VULKAN_CREATE_DLSSD_EXT1 fail: {}",
                            static_cast< int >( r ) );
            return nullptr;
        }
        return newFeature;
    }

    constexpr FramebufferImageIndex OUTPUT_IMAGE = FB_IMAGE_INDEX_UPSCALED_PONG;

    NVSDK_NGX_Resource_VK ToNGXResource( const Framebuffers&   framebuffers,
                                         uint32_t              frameIndex,
                                         FramebufferImageIndex fbImage,
                                         NVSDK_NGX_Dimensions  size,
                                         bool                  withWriteAccess = false )
    {
        auto [ image, view, format ] = framebuffers.GetImageHandles( fbImage, frameIndex );

        auto subresourceRange = VkImageSubresourceRange{
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };

        return NVSDK_NGX_Create_ImageView_Resource_VK( view,
                                                       image,
                                                       subresourceRange,
                                                       format,
                                                       size.Width,
                                                       size.Height,
                                                       withWriteAccess );
    }

}
}


auto RTGL1::DLSSRR::MakeInstance( VkDevice device, const DLSS2* dlss2 )
    -> std::shared_ptr< DLSSRR >
{
    auto inst = std::make_shared< DLSSRR >( device, dlss2 != nullptr );
    if( !inst || !inst->Valid() )
    {
        return {};
    }
    return inst;
}


auto RTGL1::DLSSRR::Apply( VkCommandBuffer               cmd,
                           uint32_t                      frameIndex,
                           Framebuffers&                 framebuffers,
                           const RenderResolutionHelper& renderResolution,
                           RgFloat2D                     jitterOffset,
                           double                        timeDelta,
                           bool                          resetAccumulation,
                           bool                          specHitDistEnabled,
                           bool                          disoccMaskEnabled,
                           bool                          exposureTexEnabled,
                           bool                          transparencyLayerEnabled,
                           const float*                  worldToViewMatrix16,
                           const float*                  viewToClipMatrix16 )
    -> FramebufferImageIndex
{
    auto label = CmdLabel{ cmd, "DLSS-RR" };

    if( !Valid() )
    {
        debug::Error( "DLSSRR: Failed to validate, Ray Reconstruction will not be applied" );
        assert( 0 );
        return OUTPUT_IMAGE;
    }

    {
        auto newResolution = renderResolution.GetResolutionState();
        // Doom64-RT: the preset is baked in at feature-create time, so changing
        // it mid-session has to re-create the feature exactly as a resize does
        // -- the DLSS2 (SR) mechanism, mirrored. Without this the cvar appears
        // to do nothing until the window changes size.
        auto newPreset = renderResolution.GetDlssRrPreset();
        if( m_prevResolution != newResolution || m_prevPreset != newPreset )
        {
            m_prevResolution = newResolution;
            m_prevPreset     = newPreset;
            m_feature = CreateDlssdFeature( m_params,
                                            m_device,
                                            cmd,
                                            newResolution,
                                            renderResolution.GetResolutionMode(),
                                            newPreset,
                                            m_feature );

            if( !m_feature )
            {
                assert( 0 );
                return OUTPUT_IMAGE;
            }
        }
    }

    constexpr FramebufferImageIndex INPUT_IMAGES[] = {
        FB_IMAGE_INDEX_FINAL,
        FB_IMAGE_INDEX_DEPTH_NDC,
        FB_IMAGE_INDEX_MOTION_DLSS,
        FB_IMAGE_INDEX_DIFF_COLOR_HISTORY,
        FB_IMAGE_INDEX_DIFF_PING_COLOR_AND_VARIANCE,
        FB_IMAGE_INDEX_DIFF_PONG_COLOR_AND_VARIANCE,
        FB_IMAGE_INDEX_RR_DISOCCLUSION,
        FB_IMAGE_INDEX_SPECULAR_HIT_DISTANCE,
        FB_IMAGE_INDEX_RR_EXPOSURE,
    };

    framebuffers.BarrierMultiple( cmd, //
                                  frameIndex,
                                  INPUT_IMAGES,
                                  Framebuffers::BarrierType::Storage );

    // Doom64-RT: the transparency layer is written as a COLOR ATTACHMENT by the
    // world raster pass, not as shader storage -- BarrierType::Storage would
    // name the wrong srcAccess (SHADER_WRITE) and leave the attachment writes
    // un-made-available. BarrierType::All includes COLOR_ATTACHMENT_WRITE.
    {
        constexpr FramebufferImageIndex ATTACHMENT_INPUTS[] = {
            FB_IMAGE_INDEX_RR_TRANSPARENCY,
        };
        framebuffers.BarrierMultiple( cmd, //
                                      frameIndex,
                                      ATTACHMENT_INPUTS,
                                      Framebuffers::BarrierType::All );
    }

    // Doom64-RT: the OUTPUT image needs a barrier too. NGX writes UPSCALED_PONG
    // in compute, but the previous frame's post-effect chain ping-pongs through
    // it with input-only barriers (EffectBase barriers only what it READS), so
    // without this there is a cross-frame WAR/WAW hazard on the same queue --
    // frequently masked by the blit chain's restore barrier, never guaranteed.
    // Storage type gives the execution dependency (graphics|RT|compute src) and
    // SHADER_WRITE availability; layout stays GENERAL, which NGX expects.
    {
        constexpr FramebufferImageIndex OUTPUT_IMAGES[] = { OUTPUT_IMAGE };
        framebuffers.BarrierMultiple( cmd, //
                                      frameIndex,
                                      OUTPUT_IMAGES,
                                      Framebuffers::BarrierType::Storage );
    }

    auto sourceSize = NVSDK_NGX_Dimensions{
        renderResolution.Width(),
        renderResolution.Height(),
    };
    auto targetSize = NVSDK_NGX_Dimensions{
        renderResolution.UpscaledWidth(),
        renderResolution.UpscaledHeight(),
    };

    // clang-format off
    NVSDK_NGX_Resource_VK colorResource     = ToNGXResource( framebuffers, frameIndex, FB_IMAGE_INDEX_FINAL, sourceSize );
    NVSDK_NGX_Resource_VK outputResource    = ToNGXResource( framebuffers, frameIndex, OUTPUT_IMAGE, targetSize, true );
    NVSDK_NGX_Resource_VK motionResource    = ToNGXResource( framebuffers, frameIndex, FB_IMAGE_INDEX_MOTION_DLSS, sourceSize );
    NVSDK_NGX_Resource_VK depthResource     = ToNGXResource( framebuffers, frameIndex, FB_IMAGE_INDEX_DEPTH_NDC, sourceSize );
    // corrected guides staged by CmNoisyCompose (ro_d * mod / envBRDF * mod), not raw Albedo/F0
    NVSDK_NGX_Resource_VK albedoResource    = ToNGXResource( framebuffers, frameIndex, FB_IMAGE_INDEX_DIFF_COLOR_HISTORY, sourceSize );
    NVSDK_NGX_Resource_VK normalsResource   = ToNGXResource( framebuffers, frameIndex, FB_IMAGE_INDEX_DIFF_PING_COLOR_AND_VARIANCE, sourceSize );
    NVSDK_NGX_Resource_VK specAlbResource   = ToNGXResource( framebuffers, frameIndex, FB_IMAGE_INDEX_DIFF_PONG_COLOR_AND_VARIANCE, sourceSize );
    NVSDK_NGX_Resource_VK disoccResource    = ToNGXResource( framebuffers, frameIndex, FB_IMAGE_INDEX_RR_DISOCCLUSION, sourceSize );
    NVSDK_NGX_Resource_VK specHitDistResource = ToNGXResource( framebuffers, frameIndex, FB_IMAGE_INDEX_SPECULAR_HIT_DISTANCE, sourceSize );
    NVSDK_NGX_Resource_VK exposureResource  = ToNGXResource( framebuffers, frameIndex, FB_IMAGE_INDEX_RR_EXPOSURE, NVSDK_NGX_Dimensions{ 1, 1 } );
    NVSDK_NGX_Resource_VK transLayerResource = ToNGXResource( framebuffers, frameIndex, FB_IMAGE_INDEX_RR_TRANSPARENCY, sourceSize );
    // clang-format on

    // Matrices must outlive Evaluate — NGX reads pointers asynchronously with the cmd buffer.
    // Copy into locals that stay alive for this call (Evaluate is sync on the GPU timeline
    // but parameter set is immediate).
    float worldToView[ 16 ]{};
    float viewToClip[ 16 ]{};
    if( worldToViewMatrix16 )
    {
        memcpy( worldToView, worldToViewMatrix16, sizeof( worldToView ) );
    }
    if( viewToClipMatrix16 )
    {
        memcpy( viewToClip, viewToClipMatrix16, sizeof( viewToClip ) );
    }

    auto evalParams = NVSDK_NGX_VK_DLSSD_Eval_Params{};
    evalParams.pInDiffuseAlbedo          = &albedoResource;
    evalParams.pInSpecularAlbedo         = &specAlbResource;
    evalParams.pInNormals                = &normalsResource;
    evalParams.pInRoughness              = nullptr; // packed into normals.w
    evalParams.pInColor                  = &colorResource;
    evalParams.pInOutput                 = &outputResource;
    evalParams.pInDepth                  = &depthResource;
    evalParams.pInMotionVectors          = &motionResource;
    evalParams.InJitterOffsetX           = jitterOffset.data[ 0 ] * ( -1 );
    evalParams.InJitterOffsetY           = jitterOffset.data[ 1 ] * ( -1 );
    evalParams.InRenderSubrectDimensions = sourceSize;
    evalParams.InReset                   = resetAccumulation ? 1 : 0;
    evalParams.InMVScaleX                = float( sourceSize.Width );
    evalParams.InMVScaleY                = float( sourceSize.Height );
    evalParams.InPreExposure             = 1.0f;
    evalParams.InExposureScale           = 1.0f;
    evalParams.InToneMapperType          = NVSDK_NGX_TONEMAPPER_ONEOVERLUMA;
    evalParams.InFrameTimeDeltaInMsec    = float( timeDelta * 1000.0 );
    // Specular hit distance: world distance from the shading point to whatever
    // produced the highlight. RR needs it because specular does not live ON the
    // surface -- without it RR reprojects highlights as if it did, so glossy
    // surfaces smear and fizzle under camera motion. This was once bound to
    // FB_DEPTH_WORLD (the primary-hit CAMERA distance -- the wrong signal) and
    // then correctly set to nullptr; FB_SPECULAR_HIT_DISTANCE is the right value,
    // resolved from distToLight by CmNoisyCompose. Gated so it can be A/B'd
    // against the nullptr behaviour without a rebuild.
    evalParams.pInSpecularHitDistance    = specHitDistEnabled ? &specHitDistResource : nullptr;
    // Sentinel 10000.0 written by CmNoisyCompose where scene lighting changed
    // sharply vs the reprojected previous frame — forces RR to drop history
    // (transient lights: barrel explosions, muzzle flashes, occluded glows).
    // Disabling the mask must UNBIND it, not merely write zeros into it. Those
    // are not the same thing to NGX: an all-zero mask is still a mask, and if
    // NGX reads it as a per-pixel history-validity signal rather than as a
    // discard sentinel, all-zero means "discard history everywhere, every
    // frame" -- RR with no temporal accumulation at all. Binding it
    // unconditionally is also why toggling rt_rr_disocc appeared to do nothing:
    // both states left the buffer bound, so the toggle never tested the
    // pre-2026-08-06 configuration (no mask at all).
    evalParams.pInDisocclusionMask       = disoccMaskEnabled ? &disoccResource : nullptr;
    // The 1x1 "final exposure scale" (nvsdk_ngx_defs.h) written by
    // CmPrepareFinal from the tonemapping buffer. Only meaningful with the
    // pre-exposure reorder: with InPreExposure = 1.0 and unexposed radiance in
    // pInColor, this is how the network learns the frame's absolute scale --
    // Doom's auto-exposure swings it ~52x between a lit hall and a dark
    // crypt, and the network is not scale-invariant. The caller gates this on
    // rrPreExposure: binding it against an ALREADY-exposed input would declare
    // the scale twice.
    evalParams.pInExposureTexture        = exposureTexEnabled ? &exposureResource : nullptr;
    // Premultiplied RGBA16F layer holding everything the world raster pass
    // drew (translucent sprites, particles, lens flares), composited by NGX
    // AFTER denoise+upscale. Without it that content is baked into pInColor
    // while every guide describes the opaque surface BEHIND it. nullptr when
    // disabled -- same unbind-vs-zero reasoning as the disocclusion mask.
    evalParams.pInTransparencyLayer      = transparencyLayerEnabled ? &transLayerResource : nullptr;
    evalParams.pInWorldToViewMatrix      = worldToView;
    evalParams.pInViewToClipMatrix       = viewToClip;

    NVSDK_NGX_Result r = NGX_VULKAN_EVALUATE_DLSSD_EXT( cmd, m_feature, m_params, &evalParams );

    if( NVSDK_NGX_FAILED( r ) )
    {
        debug::Warning( "DLSSRR: NGX_VULKAN_EVALUATE_DLSSD_EXT fail: {}", static_cast< int >( r ) );
    }
    return OUTPUT_IMAGE;
}


auto RTGL1::DLSSRR::GetOptimalSettings( uint32_t               userWidth,
                                        uint32_t               userHeight,
                                        RgRenderResolutionMode mode ) const
    -> std::pair< uint32_t, uint32_t >
{
    if( !Valid() )
    {
        assert( 0 );
        return { userWidth, userHeight };
    }

    uint32_t renderWidth  = userWidth;
    uint32_t renderHeight = userHeight;
    uint32_t minWidth     = userWidth;
    uint32_t minHeight    = userHeight;
    uint32_t maxWidth     = userWidth;
    uint32_t maxHeight    = userHeight;
    float    sharpness    = 1.0f;

    NVSDK_NGX_Result r = NGX_DLSSD_GET_OPTIMAL_SETTINGS( m_params,
                                                         userWidth,
                                                         userHeight,
                                                         ToNGXPerfQuality( mode ),
                                                         &renderWidth,
                                                         &renderHeight,
                                                         &maxWidth,
                                                         &maxHeight,
                                                         &minWidth,
                                                         &minHeight,
                                                         &sharpness );
    if( NVSDK_NGX_FAILED( r ) )
    {
        debug::Warning( "DLSSRR: NGX_DLSSD_GET_OPTIMAL_SETTINGS fail: {}",
                        static_cast< int >( r ) );
        assert( 0 );
        return { userWidth, userHeight };
    }
    return { renderWidth, renderHeight };
}

#else

RTGL1::DLSSRR::DLSSRR( VkDevice, bool ) {}

RTGL1::DLSSRR::~DLSSRR() = default;

bool RTGL1::DLSSRR::Valid() const
{
    return false;
}

auto RTGL1::DLSSRR::MakeInstance( VkDevice, const DLSS2* ) -> std::shared_ptr< DLSSRR >
{
    return {};
}

auto RTGL1::DLSSRR::Apply( VkCommandBuffer,
                           uint32_t,
                           Framebuffers&,
                           const RenderResolutionHelper&,
                           RgFloat2D,
                           double,
                           bool,
                           bool, // specHitDistEnabled     -- the stub must match
                           bool, // disoccMaskEnabled      -- the 13-param
                           bool, // exposureTexEnabled     -- declaration or a
                           bool, // transparencyLayerEnabled -- build without
                                 // RG_USE_NATIVE_DLSS2 cannot compile
                                 // (out-of-line def with no decl)
                           const float*,
                           const float* ) -> FramebufferImageIndex
{
    return FB_IMAGE_INDEX_UPSCALED_PONG;
}

auto RTGL1::DLSSRR::GetOptimalSettings( uint32_t userWidth,
                                        uint32_t userHeight,
                                        RgRenderResolutionMode ) const
    -> std::pair< uint32_t, uint32_t >
{
    return { userWidth, userHeight };
}

#endif
