// Copyright (c) 2020-2021 Sultim Tsyrendashiev
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

namespace RTGL1
{
struct Devmode
{
    struct DebugPrim
    {
        std::optional< UploadResult > result;
        uint32_t                      callIndex;
        uint64_t                      objectId;
        std::string                   meshName;
        uint32_t                      primitiveIndex;
        std::string                   primitiveName;
        std::string                   textureName;
    };

    enum class DebugPrimMode : int
    {
        None,
        RayTraced,
        Rasterized,
        NonWorld,
        Decal,
    };

    bool     debugWindowOnTop{ false };
    bool     reloadShaders{ false };
    uint32_t debugShowFlags{ 0 };

    // ImGui FontGlobalScale (base TTF stays 15px). Persisted.
    float fontGlobalScale{ 1.f };

    // Materials A/B (live; no Override). Persisted. Split for RR diagnosis.
    bool materialStripNormals{ false };
    bool materialStripMetallic{ false };
    bool materialStripRoughness{ false };
    bool materialStripHeight{ false };
    bool materialStripEmissives{ false };
    // Live mix: authored roughness → fully matte (1). Visible A/B after ORM clamp;
    // minRoughness floor alone is a no-op when maps already sit ~0.82+.
    float roughnessTowardMatte{ 0.f };

    // Dirty tracking for debounce-save of rt/devmode_settings.json
    bool   settingsDirty{ false };
    double settingsDirtyAt{ 0.0 };

    bool antiFirefly{ true };
    // DLSS-RR: A-SVGF temporal before ComposeNoisy. Default OFF — ghosted duplicate view.
    bool rrTemporalPrefilter{ false };
    // Once true, Dev checkbox wins over game cvar until "Use game cvar" is pressed.
    bool rrTemporalPrefilterSticky{ false };

    // Live RR/denoise knobs (always available; sticky wins over game draw params).
    bool  illumSensSticky{ false };
    float illumSensDirect{ 1.f };
    float illumSensIndirect{ 0.75f };
    float illumSensSpec{ 1.f };

    // Sticky DLSS Ray Reconstruction toggle (Present path); when sticky, overrides start-frame.
    bool rayReconstruction{ false };
    bool rayReconstructionSticky{ false };

    bool fluidStopVisualize{ false };

    struct
    {
        // IMPORTANT: must be value-initialized. Without defaults, `enable` /
        // `pixelizedEnable` / `upscaleTechnique` are indeterminate after
        // make_unique<Devmode>(), so Override randomly activates with Nearest/
        // Linear + pixelized → noisy PT + blocky HUD (intermittent).
        bool enable{ false };

        int   maxBounceShadows{ 0 };
        int   indirectBounces{ 2 };
        bool  indirectLegacyBounceWeight{ true };
        float directDiffuseSensitivityToChange{ 1.f };
        float indirectDiffuseSensitivityToChange{ 1.f };
        float specularSensitivityToChange{ 1.f };

        bool  disableEyeAdaptation{ false };
        float ev100Min{ 0.f };
        float ev100Max{ 0.f };
        float saturation[ 3 ]{ 1.f, 1.f, 1.f };
        float crosstalk[ 3 ]{ 0.f, 0.f, 0.f };

        bool                     vsync{ false };
        RgFrameGenerationMode    frameGeneration{ RG_FRAME_GENERATION_MODE_OFF };
        bool                     preferDxgiPresent{ true };
        bool                     hdr{ false };
        RgRenderUpscaleTechnique upscaleTechnique{ RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS };
        RgRenderSharpenTechnique sharpenTechnique{ RG_RENDER_SHARPEN_TECHNIQUE_NONE };
        RgRenderResolutionMode   resolutionMode{ RG_RENDER_RESOLUTION_MODE_BALANCED };
        float                    customRenderSizeScale{ 1.f };
        bool                     pixelizedEnable{ false };
        int                      pixelizedHeight{ 480 };
        bool                     rayReconstruction{ false };

        float normalMapStrength{ 1.f };
        float heightMapDepth{ 1.f };
        float emissionMapBoost{ 1.f };
        float emissionMaxScreenColor{ 1.f };

        float lightmapScreenCoverage{ 0.f };
        bool  fluidEnabled{ false };
        bool  fluidReset{ false };
        RgFloat3D fluidGravity{ 0.f, 0.f, -14.f };

        bool allowMapAutoExport{ false };

    } drawInfoOvrd{};

    struct
    {
        bool                       fovEnable{ false };
        float                      fovDeg{ 90.f };
        bool                       customEnable{ false };
        RgFloat3D                  customPos{ 0.f, 0.f, 0.f };
        RgFloat2D                  customAngles{ 0.f, 0.f };
        RgFloat2D                  intr_lastAngles{ 0.f, 0.f };
        std::optional< RgFloat2D > intr_lastMouse{};
        float                      intr_time{ 0.f };
    } cameraOvrd{};

    bool ignoreExternalGeometry{ false };
    bool allowExportOfExistingReplacements{ false };

    bool materialsTableEnable{ false };

    DebugPrimMode            primitivesTableMode{ DebugPrimMode::None };
    std::vector< DebugPrim > primitivesTable{};

    bool breakOnTexturePrimitive{ false };
    bool breakOnTextureImage{ false };
    char breakOnTexture[ 256 ];

    RgMessageSeverityFlags logFlags{ RG_MESSAGE_SEVERITY_VERBOSE | RG_MESSAGE_SEVERITY_INFO |
                                     RG_MESSAGE_SEVERITY_WARNING | RG_MESSAGE_SEVERITY_ERROR };
    bool                   logAutoScroll{ true };
    std::deque< std::tuple< RgMessageSeverityFlags, uint32_t /* count */, std::string > > logs{};
};

}
