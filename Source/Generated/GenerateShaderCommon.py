# Copyright (c) 2020-2021 Sultim Tsyrendashiev
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# This script generates two separate header files for C and GLSL but with identical data

import sys
import os
import re
from math import log2


TYPE_FLOAT32    = 0
TYPE_INT32      = 1
TYPE_UINT32     = 2


C_TYPE_NAMES = {
    TYPE_FLOAT32:       "float",
    TYPE_INT32:         "int32_t",
    TYPE_UINT32:        "uint32_t",
}

GLSL_TYPE_NAMES = {
    TYPE_FLOAT32:       "float",
    TYPE_INT32:         "int",
    TYPE_UINT32:        "uint",
    (TYPE_FLOAT32,  2): "vec2",
    (TYPE_FLOAT32,  3): "vec3",
    (TYPE_FLOAT32,  4): "vec4",
    (TYPE_INT32,    2): "ivec2",
    (TYPE_INT32,    3): "ivec3",
    (TYPE_INT32,    4): "ivec4",
    (TYPE_UINT32,   2): "uvec2",
    (TYPE_UINT32,   3): "uvec3",
    (TYPE_UINT32,   4): "uvec4",
    (TYPE_FLOAT32, 22): "mat2",
    (TYPE_FLOAT32, 23): "mat2x3",
    (TYPE_FLOAT32, 32): "mat3x2",
    (TYPE_FLOAT32, 33): "mat3",
    (TYPE_FLOAT32, 34): "mat3x4",
    (TYPE_FLOAT32, 43): "mat4x3",
    (TYPE_FLOAT32, 44): "mat4",
}


TYPE_ACTUAL_SIZES = {
    TYPE_FLOAT32:   4,
    TYPE_INT32:     4,
    TYPE_UINT32:    4,
    (TYPE_FLOAT32,  2): 8,
    (TYPE_FLOAT32,  3): 12,
    (TYPE_FLOAT32,  4): 16,
    (TYPE_INT32,    2): 8,
    (TYPE_INT32,    3): 12,
    (TYPE_INT32,    4): 16,
    (TYPE_UINT32,   2): 8,
    (TYPE_UINT32,   3): 12,
    (TYPE_UINT32,   4): 16,
    (TYPE_FLOAT32, 22): 16,
    (TYPE_FLOAT32, 23): 24,
    (TYPE_FLOAT32, 32): 24,
    (TYPE_FLOAT32, 33): 36,
    (TYPE_FLOAT32, 34): 48,
    (TYPE_FLOAT32, 43): 48,
    (TYPE_FLOAT32, 44): 64,
}

GLSL_TYPE_SIZES_STD_430 = {
    TYPE_FLOAT32:   4,
    TYPE_INT32:     4,
    TYPE_UINT32:    4,
    (TYPE_FLOAT32,  2): 8,
    (TYPE_FLOAT32,  3): 16,
    (TYPE_FLOAT32,  4): 16,
    (TYPE_INT32,    2): 8,
    (TYPE_INT32,    3): 16,
    (TYPE_INT32,    4): 16,
    (TYPE_UINT32,   2): 8,
    (TYPE_UINT32,   3): 16,
    (TYPE_UINT32,   4): 16,
    (TYPE_FLOAT32, 22): 16,
    (TYPE_FLOAT32, 23): 24,
    (TYPE_FLOAT32, 32): 24,
    (TYPE_FLOAT32, 33): 36,
    (TYPE_FLOAT32, 34): 48,
    (TYPE_FLOAT32, 43): 48,
    (TYPE_FLOAT32, 44): 64,
}


# These types are only for image format use!
TYPE_UNORM8     = 3
TYPE_UINT8      = 4
TYPE_UINT16     = 5
TYPE_FLOAT16    = 6
TYPE_PACK_11    = 128   # R11G11B10
TYPE_PACK_E5    = 129   # shared exponent, E5B9G9R9

COMPONENT_R     = 0
COMPONENT_RG    = 1
COMPONENT_RGB   = 2
COMPONENT_RGBA  = 3


VULKAN_IMAGE_FORMATS = {
    (TYPE_UNORM8,   COMPONENT_R):       "VK_FORMAT_R8_UNORM",
    (TYPE_UNORM8,   COMPONENT_RG):      "VK_FORMAT_R8G8_UNORM",
    (TYPE_UNORM8,   COMPONENT_RGBA):    "VK_FORMAT_R8G8B8A8_UNORM",

    (TYPE_UINT8,    COMPONENT_R):       "VK_FORMAT_R8_UINT",
    (TYPE_UINT8,    COMPONENT_RG):      "VK_FORMAT_R8G8_UINT",
    (TYPE_UINT8,    COMPONENT_RGBA):    "VK_FORMAT_R8G8B8A8_UINT",

    (TYPE_UINT16,   COMPONENT_R):       "VK_FORMAT_R16_UINT",
    (TYPE_UINT16,   COMPONENT_RG):      "VK_FORMAT_R16G16_UINT",
    (TYPE_UINT16,   COMPONENT_RGBA):    "VK_FORMAT_R16G16B16A16_UINT",

    (TYPE_UINT32,   COMPONENT_R):       "VK_FORMAT_R32_UINT",
    (TYPE_UINT32,   COMPONENT_RG):      "VK_FORMAT_R32G32_UINT",
    (TYPE_UINT32,   COMPONENT_RGBA):    "VK_FORMAT_R32G32B32A32_UINT",

    (TYPE_FLOAT16,  COMPONENT_R):       "VK_FORMAT_R16_SFLOAT",
    (TYPE_FLOAT16,  COMPONENT_RG):      "VK_FORMAT_R16G16_SFLOAT",
    (TYPE_FLOAT16,  COMPONENT_RGBA):    "VK_FORMAT_R16G16B16A16_SFLOAT",

    (TYPE_FLOAT32,  COMPONENT_R):       "VK_FORMAT_R32_SFLOAT",
    (TYPE_FLOAT32,  COMPONENT_RG):      "VK_FORMAT_R32G32_SFLOAT",
    (TYPE_FLOAT32,  COMPONENT_RGBA):    "VK_FORMAT_R32G32B32A32_SFLOAT",

    (TYPE_PACK_11,  COMPONENT_RGB):    "VK_FORMAT_B10G11R11_UFLOAT_PACK32",
    # Should've been VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, 
    # but not enough devices support storage images with such format.
    # So pack/unpack is done manually.
    (TYPE_PACK_E5,  COMPONENT_RGB):    "VK_FORMAT_R32_UINT",
}

GLSL_IMAGE_FORMATS = {
    (TYPE_UNORM8,   COMPONENT_R):       "r8",
    (TYPE_UNORM8,   COMPONENT_RG):      "rg8",
    (TYPE_UNORM8,   COMPONENT_RGBA):    "rgba8",

    (TYPE_UINT8,    COMPONENT_R):       "r8ui",
    (TYPE_UINT8,    COMPONENT_RG):      "rg8ui",
    (TYPE_UINT8,    COMPONENT_RGBA):    "rgba8ui",

    (TYPE_UINT16,   COMPONENT_R):       "r16ui",
    (TYPE_UINT16,   COMPONENT_RG):      "rg16ui",
    (TYPE_UINT16,   COMPONENT_RGBA):    "rgba16ui",

    (TYPE_UINT32,   COMPONENT_R):       "r32ui",
    (TYPE_UINT32,   COMPONENT_RG):      "rg32ui",
    (TYPE_UINT32,   COMPONENT_RGBA):    "rgba32ui",

    (TYPE_FLOAT16,  COMPONENT_R):       "r16f",
    (TYPE_FLOAT16,  COMPONENT_RG):      "rg16f",
    (TYPE_FLOAT16,  COMPONENT_RGBA):    "rgba16f",

    (TYPE_FLOAT32,  COMPONENT_R):       "r32f",
    (TYPE_FLOAT32,  COMPONENT_RG):      "rg32f",
    (TYPE_FLOAT32,  COMPONENT_RGBA):    "rgba32f",

    (TYPE_PACK_11,  COMPONENT_RGB):    "r11f_g11f_b10f",
    (TYPE_PACK_E5,  COMPONENT_RGB):    "r32ui",
}

GLSL_IMAGE_2D_TYPE = { 
    TYPE_FLOAT32    : "image2D",
    TYPE_INT32      : "iimage2D",
    TYPE_UINT32     : "uimage2D",
    TYPE_UNORM8     : "image2D",
    TYPE_UINT8      : "uimage2D",
    TYPE_UINT16     : "uimage2D",
    TYPE_FLOAT16    : "image2D",
    TYPE_PACK_11    : "image2D",
    TYPE_PACK_E5    : "uimage2D",
}

GLSL_SAMPLER_2D_TYPE = { 
    TYPE_FLOAT32    : "sampler2D",
    TYPE_INT32      : "isampler2D",
    TYPE_UINT32     : "usampler2D",
    TYPE_UNORM8     : "sampler2D",
    TYPE_UINT8      : "usampler2D",
    TYPE_UINT16     : "usampler2D",
    TYPE_FLOAT16    : "sampler2D",
    TYPE_PACK_11    : "sampler2D",
    TYPE_PACK_E5    : "usampler2D",
}


USE_BASE_STRUCT_NAME_IN_VARIABLE_STRIDE = False
USE_MULTIDIMENSIONAL_ARRAYS_IN_C = False
CONST_TO_EVALUATE = "CONST VALUE MUST BE EVALUATED"







# --------------------------------------------------------------------------------------------- #
# User defined constants
# --------------------------------------------------------------------------------------------- #

GRADIENT_ESTIMATION_ENABLED = True
FRAMEBUF_IGNORE_ATTACHMENTS_DEFINE = "FRAMEBUF_IGNORE_ATTACHMENTS" # define this, to not specify framebufs that are used as attachments
def BIT( i ):
    return "1 << " + str( i )

CONST = {
    "MAX_INSTANCE_COUNT"                    : 1 << 16,
    "MAX_GEOM_INFO_COUNT"                   : 1 << 17,
    
    "BINDING_VERTEX_BUFFER_STATIC"              : 0,
    "BINDING_VERTEX_BUFFER_DYNAMIC"             : 1,
    "BINDING_INDEX_BUFFER_STATIC"               : 2,
    "BINDING_INDEX_BUFFER_DYNAMIC"              : 3,
    "BINDING_GEOMETRY_INSTANCES"                : 4,
    "BINDING_GEOMETRY_INSTANCES_MATCH_PREV"     : 5,
    "BINDING_PREV_POSITIONS_BUFFER_DYNAMIC"     : 6,
    "BINDING_PREV_INDEX_BUFFER_DYNAMIC"         : 7,
    "BINDING_STATIC_TEXCOORD_LAYER_1"           : 8,
    "BINDING_STATIC_TEXCOORD_LAYER_2"           : 9,
    "BINDING_STATIC_TEXCOORD_LAYER_3"           : 10,
    "BINDING_DYNAMIC_TEXCOORD_LAYER_1"          : 11,
    "BINDING_DYNAMIC_TEXCOORD_LAYER_2"          : 12,
    "BINDING_DYNAMIC_TEXCOORD_LAYER_3"          : 13,
    "BINDING_GLOBAL_UNIFORM"                    : 0,
    "BINDING_ACCELERATION_STRUCTURE_MAIN"       : 0,
    "BINDING_TEXTURES"                          : 0,
    "BINDING_CUBEMAPS"                          : 0,
    "BINDING_RENDER_CUBEMAP"                    : 0,
    "BINDING_BLUE_NOISE"                        : 0,
    "BINDING_LUM_HISTOGRAM"                     : 0,
    "BINDING_LIGHT_SOURCES"                     : 0,
    "BINDING_LIGHT_SOURCES_PREV"                : 1,
    "BINDING_LIGHT_SOURCES_INDEX_PREV_TO_CUR"   : 2,
    "BINDING_LIGHT_SOURCES_INDEX_CUR_TO_PREV"   : 3,
    "BINDING_INITIAL_LIGHTS_GRID"               : 4,
    "BINDING_INITIAL_LIGHTS_GRID_PREV"          : 5,
    "BINDING_LENS_FLARES_CULLING_INPUT"         : 0,
    "BINDING_LENS_FLARES_DRAW_CMDS"             : 1,
    "BINDING_DRAW_LENS_FLARES_INSTANCES"        : 0,
    "BINDING_PORTAL_INSTANCES"                  : 0,
    "BINDING_LPM_PARAMS"                        : 0,
    "BINDING_RESTIR_INDIRECT_RESERVOIRS"        : 2,
    "BINDING_RESTIR_INDIRECT_RESERVOIRS_PREV"   : 1,
    "BINDING_VOLUMETRIC_STORAGE"                : 0,
    "BINDING_VOLUMETRIC_SAMPLER"                : 1,
    "BINDING_VOLUMETRIC_SAMPLER_PREV"           : 2,
    # Doom64-RT: uncommented alongside ILLUMINATION_VOLUME=1 above. Volumetric.cpp's
    # #if ILLUMINATION_VOLUME descriptor-set blocks (image + sampler for the
    # illumination volume) reference these two names; with the define on and these
    # commented out, the shader/C++ compile fails on "undeclared identifier" -- this
    # dict is a single source shared by GLSL and C++ headers, so both need it live
    # together, not just the top-level ILLUMINATION_VOLUME flag (2026-08-08).
    "BINDING_VOLUMETRIC_ILLUMINATION"           : 3,
    "BINDING_VOLUMETRIC_ILLUMINATION_SAMPLER"   : 4,
    "BINDING_FLUID_PARTICLES_ARRAY"             : 0,
    "BINDING_FLUID_GENERATE_ID_TO_SOURCE"       : 1,
    "BINDING_FLUID_SOURCES"                     : 2,

    "INSTANCE_CUSTOM_INDEX_FLAG_FIRST_PERSON"           : BIT( 0 ),
    "INSTANCE_CUSTOM_INDEX_FLAG_FIRST_PERSON_VIEWER"    : BIT( 1 ),
    "INSTANCE_CUSTOM_INDEX_FLAG_SKY"                    : BIT( 2 ),

    "INSTANCE_MASK_WORLD_0"                 : BIT( 0 ),
    "INSTANCE_MASK_WORLD_1"                 : BIT( 1 ),
    "INSTANCE_MASK_WORLD_2"                 : BIT( 2 ),
    "INSTANCE_MASK_RESERVED_0"              : BIT( 3 ),
    "INSTANCE_MASK_RESERVED_1"              : BIT( 4 ),
    "INSTANCE_MASK_REFRACT"                 : BIT( 5 ),
    "INSTANCE_MASK_FIRST_PERSON"            : BIT( 6 ),
    "INSTANCE_MASK_FIRST_PERSON_VIEWER"     : BIT( 7 ),
    
    "PAYLOAD_INDEX_DEFAULT"                 : 0,
    "PAYLOAD_INDEX_SHADOW"                  : 1,
    
    "SBT_INDEX_RAYGEN_PRIMARY"              : 0,
    "SBT_INDEX_RAYGEN_REFL_REFR"            : 1,
    "SBT_INDEX_RAYGEN_DIRECT"               : 2,
    "SBT_INDEX_RAYGEN_INDIRECT_INIT"        : 3,
#   "SBT_INDEX_RAYGEN_INDIRECT_FINAL"       :  ,
    "SBT_INDEX_RAYGEN_GRADIENTS"            : 4,
    "SBT_INDEX_RAYGEN_INITIAL_RESERVOIRS"   : 5,
    "SBT_INDEX_RAYGEN_VOLUMETRIC"           : 6,
    "SBT_INDEX_MISS_DEFAULT"                : 0,
    "SBT_INDEX_MISS_SHADOW"                 : 1,
    "SBT_INDEX_HITGROUP_FULLY_OPAQUE"       : 0,
    "SBT_INDEX_HITGROUP_ALPHA_TESTED"       : 1,
    
    "MATERIAL_NO_TEXTURE"                   : 0,

    "MATERIAL_BLENDING_TYPE_OPAQUE"         : 0,
    "MATERIAL_BLENDING_TYPE_ALPHA"          : 1,
    "MATERIAL_BLENDING_TYPE_ADD"            : 2,
    "MATERIAL_BLENDING_TYPE_SHADE"          : 3,
    "MATERIAL_BLENDING_TYPE_BIT_COUNT"      : 2,
    "MATERIAL_BLENDING_TYPE_BIT_MASK"       : CONST_TO_EVALUATE,

    "GEOM_INST_FLAG_BLENDING_LAYER_COUNT"   : 4,         
    # first 8 bits (MATERIAL_BLENDING_TYPE_BIT_COUNT * GEOM_INST_FLAG_BLENDING_LAYER_COUNT)
    # are for the blending flags per each layer, others can be used
    "GEOM_INST_FLAG_NO_WATER_CAUSTICS"      : BIT( 8 ),
    # Doom64-RT: 2-bit liquid index (water / nukage / sludge / blood), read by
    # the stylized water surface to pick its body and crest colour. 0 = water,
    # so anything that only sets GEOM_INST_FLAG_MEDIA_TYPE_WATER is unchanged.
    "GEOM_INST_FLAG_LIQUID_BIT0"            : BIT( 9 ),
    "GEOM_INST_FLAG_LIQUID_BIT1"            : BIT( 10 ),
    # Doom64-RT: lava surface. See RG_MESH_PRIMITIVE_LAVA.
    "GEOM_INST_FLAG_LAVA"                   : BIT( 11 ),
    "GEOM_INST_FLAG_RESERVED_4"             : BIT( 12 ),
    "GEOM_INST_FLAG_GLASS_IF_SMOOTH"        : BIT( 13 ),
    "GEOM_INST_FLAG_MIRROR_IF_SMOOTH"       : BIT( 14 ),
    "GEOM_INST_FLAG_EXISTS_LAYER1"          : BIT( 15 ),
    "GEOM_INST_FLAG_EXISTS_LAYER2"          : BIT( 16 ),
    "GEOM_INST_FLAG_EXISTS_LAYER3"          : BIT( 17 ),
    "GEOM_INST_FLAG_MEDIA_TYPE_ACID"        : BIT( 18 ),
    "GEOM_INST_FLAG_EXACT_NORMALS"          : BIT( 19 ),
    "GEOM_INST_FLAG_IGNORE_REFRACT_AFTER"   : BIT( 20 ),
    "GEOM_INST_FLAG_RESERVED_5"             : BIT( 21 ),
    "GEOM_INST_FLAG_RESERVED_6"             : BIT( 22 ),
    "GEOM_INST_FLAG_THIN_MEDIA"             : BIT( 23 ),
    "GEOM_INST_FLAG_REFRACT"                : BIT( 24 ),
    "GEOM_INST_FLAG_REFLECT"                : BIT( 25 ),
    "GEOM_INST_FLAG_PORTAL"                 : BIT( 26 ),
    "GEOM_INST_FLAG_MEDIA_TYPE_WATER"       : BIT( 27 ),
    "GEOM_INST_FLAG_MEDIA_TYPE_GLASS"       : BIT( 28 ),
    "GEOM_INST_FLAG_GENERATE_NORMALS"       : BIT( 29 ),
    "GEOM_INST_FLAG_INVERTED_NORMALS"       : BIT( 30 ),
    "GEOM_INST_FLAG_IS_DYNAMIC"             : BIT( 31 ),

    "SKY_TYPE_COLOR"                        : 0,
    "SKY_TYPE_CUBEMAP"                      : 1,
    "SKY_TYPE_RASTERIZED_GEOMETRY"          : 2,

    # to reduce shader opearations
    "SUPPRESS_TEXLAYERS"                    : 1,
    
    "BLUE_NOISE_TEXTURE_COUNT"              : 128,
    "BLUE_NOISE_TEXTURE_SIZE"               : 128,
    "BLUE_NOISE_TEXTURE_SIZE_POW"           : CONST_TO_EVALUATE,

    "COMPUTE_COMPOSE_GROUP_SIZE_X"          : 16,
    "COMPUTE_COMPOSE_GROUP_SIZE_Y"          : 16,

    "COMPUTE_DECAL_APPLY_GROUP_SIZE_X"      : 16,
    
    "COMPUTE_BLOOM_UPSAMPLE_GROUP_SIZE_X"   : 16,
    "COMPUTE_BLOOM_UPSAMPLE_GROUP_SIZE_Y"   : 16,
    "COMPUTE_BLOOM_DOWNSAMPLE_GROUP_SIZE_X" : 16,
    "COMPUTE_BLOOM_DOWNSAMPLE_GROUP_SIZE_Y" : 16,
    "COMPUTE_BLOOM_APPLY_GROUP_SIZE_X"      : 16,
    "COMPUTE_BLOOM_APPLY_GROUP_SIZE_Y"      : 16,
    "COMPUTE_BLOOM_STEP_COUNT"              : 7,

    "COMPUTE_EFFECT_GROUP_SIZE_X"           : 16,
    "COMPUTE_EFFECT_GROUP_SIZE_Y"           : 16,

    "COMPUTE_LUM_HISTOGRAM_GROUP_SIZE_X"    : 16,
    "COMPUTE_LUM_HISTOGRAM_GROUP_SIZE_Y"    : 16,
    "COMPUTE_LUM_HISTOGRAM_BIN_COUNT"       : 256,

    "COMPUTE_VERT_PREPROC_GROUP_SIZE_X"     : 256,
    "VERT_PREPROC_MODE_ONLY_DYNAMIC"        : 0,
    "VERT_PREPROC_MODE_DYNAMIC_AND_MOVABLE" : 1,
    "VERT_PREPROC_MODE_ALL"                 : 2,

    "GRADIENT_ESTIMATION_ENABLED"           : int(GRADIENT_ESTIMATION_ENABLED),
    "COMPUTE_GRADIENT_ATROUS_GROUP_SIZE_X"  : 16,
    "COMPUTE_ANTIFIREFLY_GROUP_SIZE_X"      : 16,
    "COMPUTE_SVGF_TEMPORAL_GROUP_SIZE_X"    : 16,
    "COMPUTE_SVGF_VARIANCE_GROUP_SIZE_X"    : 16,
    "COMPUTE_SVGF_ATROUS_GROUP_SIZE_X"      : 16,
    "COMPUTE_SVGF_ATROUS_ITERATION_COUNT"   : 4,

    "COMPUTE_ASVGF_STRATA_SIZE"                         : 3,
    "COMPUTE_ASVGF_GRADIENT_ATROUS_ITERATION_COUNT"     : 4,  

    "COMPUTE_INDIRECT_DRAW_FLARES_GROUP_SIZE_X"         : 256,
    "LENS_FLARES_MAX_DRAW_CMD_COUNT"                    : 512,

    "COMPUTE_FLUID_PARTICLES_GROUP_SIZE_X"              : 256,
    "COMPUTE_FLUID_PARTICLES_GENERATE_GROUP_SIZE_X"     : 256,

    "DEBUG_SHOW_FLAG_MOTION_VECTORS"        : BIT( 0 ),
    "DEBUG_SHOW_FLAG_GRADIENTS"             : BIT( 1 ),
    "DEBUG_SHOW_FLAG_UNFILTERED_DIFFUSE"    : BIT( 2 ),
    "DEBUG_SHOW_FLAG_UNFILTERED_SPECULAR"   : BIT( 3 ),
    "DEBUG_SHOW_FLAG_UNFILTERED_INDIRECT"   : BIT( 4 ),
    "DEBUG_SHOW_FLAG_ONLY_DIRECT_DIFFUSE"   : BIT( 5 ),
    "DEBUG_SHOW_FLAG_ONLY_SPECULAR"         : BIT( 6 ),
    "DEBUG_SHOW_FLAG_ONLY_INDIRECT_DIFFUSE" : BIT( 7 ),
    "DEBUG_SHOW_FLAG_LIGHT_GRID"            : BIT( 8 ),
    "DEBUG_SHOW_FLAG_ALBEDO_WHITE"          : BIT( 9 ),
    "DEBUG_SHOW_FLAG_NORMALS"               : BIT( 10 ),
    "DEBUG_SHOW_FLAG_BLOOM"                 : BIT( 11 ),
    
    "MAX_RAY_LENGTH"                        : "10000.0",

    "MEDIA_TYPE_VACUUM"                     : 0,
    "MEDIA_TYPE_WATER"                      : 1,
    "MEDIA_TYPE_GLASS"                      : 2,
    "MEDIA_TYPE_ACID"                       : 3,
    "MEDIA_TYPE_COUNT"                      : 4,

    "GEOM_INST_NO_TRIANGLE_INFO"            : "UINT32_MAX",

    "LIGHT_TYPE_NONE"                       : 0,
    "LIGHT_TYPE_DIRECTIONAL"                : 1,
    "LIGHT_TYPE_SPHERE"                     : 2,
    "LIGHT_TYPE_SPOT"                       : 3,
    "LIGHT_TYPE_TRIANGLE"                   : 4,

    "TRIANGLE_LIGHTS"                       : 0,

    "LIGHT_ARRAY_DIRECTIONAL_LIGHT_OFFSET"  : 0,
    "LIGHT_ARRAY_REGULAR_LIGHTS_OFFSET"     : 1,

    "LIGHT_INDEX_NONE"                      : ((1 << 15) - 1),

    "LIGHT_GRID_ENABLED"                    : 0, # no effect on enabling?
#   "LIGHT_GRID_SIZE_X"                     : 16,
#   "LIGHT_GRID_SIZE_Y"                     : 16,
#   "LIGHT_GRID_SIZE_Z"                     : 16,
#   "LIGHT_GRID_CELL_SIZE"                  : 128,
#   "COMPUTE_LIGHT_GRID_GROUP_SIZE_X"       : 256,

    "PORTAL_INDEX_NONE"                     : 63,
    "PORTAL_MAX_COUNT"                      : 63,

    "PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS" : 5,

    "VOLUMETRIC_SIZE_X"                     : 160,
    "VOLUMETRIC_SIZE_Y"                     : 88,
    "VOLUMETRIC_SIZE_Z"                     : 64,
    "COMPUTE_VOLUMETRIC_GROUP_SIZE_X"       : 16,
    "COMPUTE_VOLUMETRIC_GROUP_SIZE_Y"       : 16,
    "COMPUTE_SCATTER_ACCUM_GROUP_SIZE_X"    : 16,

    # Doom64-RT: capacity of the localised-smoke puff list. The puffs ride in
    # the global uniform rather than a storage buffer, so this is a hard limit
    # and must match RG_MAX_SMOKE_PUFFS in Include/RTGL1/RTGL1.h.
    #
    # 32 -> 128 when ambient sources arrived. Muzzle smoke alone fits in 32; a
    # room full of torches does not, and the pool's overflow rule is oldest-out,
    # so an undersized buffer does not degrade -- it deletes whichever smoke is
    # oldest, which is the player's.
    #
    # THE COST IS 48 BYTES A PUFF (three vec4 arrays), so this takes the whole
    # ShGlobalUniform from 3552 to 8160 bytes. That is still under the 16384 the
    # Vulkan spec GUARANTEES for maxUniformBufferRange, which is the number that
    # matters -- a bigger cap would need the storage-buffer rewrite instead.
    "SMOKE_PUFF_MAX"                        : 128,

    "VOLUME_ENABLE_NONE"                    : 0,
    "VOLUME_ENABLE_SIMPLE"                  : 1,
    "VOLUME_ENABLE_VOLUMETRIC"              : 2,

    "HDR_DISPLAY_NONE"                      : 0,
    "HDR_DISPLAY_LINEAR"                    : 1,
    "HDR_DISPLAY_ST2084"                    : 2,

    # Doom64-RT: turned on so RsWorld.inl's rasterized-primitive branch can sample the
    # illumination volume (globalUniform.illumVolumeEnable) instead of the
    # max(1, avgLuminance) fallback. That branch is what lets a see-through sprite
    # (spectre, nightmare imp) darken with the room while the sprite stays translucent
    # and its _e emissive stays untouched -- ldrEmis is computed from baseColor() AFTER
    # this multiply, so the eyes never see the dimming. Was 0 upstream (2026-08-08).
    "ILLUMINATION_VOLUME"                   : 1,

    "COMPUTE_INDIRECT_FINAL_GROUP_SIZE_X"   : 16,
    "COMPUTE_INDIRECT_FINAL_GROUP_SIZE_Y"   : 16,
}

CONST_GLSL_ONLY = {
    "SURFACE_POSITION_INCORRECT"            : 10000000.0,  
}


def align(c, alignment):
    return ((c + alignment - 1) // alignment) * alignment


def align4(a):
    return ((a + 3) >> 2) << 2


def evalConst():
    # flags for each layer
    assert CONST[ "MATERIAL_BLENDING_TYPE_BIT_COUNT" ] * CONST[ "GEOM_INST_FLAG_BLENDING_LAYER_COUNT" ] <= 8
    CONST[ "MATERIAL_BLENDING_TYPE_BIT_MASK" ]          = ( 1 << int( CONST[ "MATERIAL_BLENDING_TYPE_BIT_COUNT" ] ) ) - 1

    CONST[ "BLUE_NOISE_TEXTURE_SIZE_POW" ]              = int( log2( CONST[ "BLUE_NOISE_TEXTURE_SIZE" ] ) )

    assert len( [ None for _, v in CONST.items() if v == CONST_TO_EVALUATE ] ) == 0, "All CONST_TO_EVALUATE values must be calculated"


# --------------------------------------------------------------------------------------------- #
# User defined structs
# --------------------------------------------------------------------------------------------- #
# Each member is defined as a tuple (base type, dimensions, name, count).
# Dimensions:   1 - scalar, 
#               2 - gvec2,
#               3 - gvec3, 
#               4 - gvec4, 
#               [xy] - gmat[xy] (i.e. 32 - gmat32)
# If count > 1 and dimensions is 2, 3 or 4 (matrices are not supported)
# then it'll be represented as an array with size (count*dimensions).

VERTEX_STRUCT = [
    (TYPE_FLOAT32,      3,     "position",              1),
    (TYPE_UINT32 ,      1,     "normalPacked",          1),
    (TYPE_FLOAT32,      2,     "texCoord",              1),
    (TYPE_UINT32,       1,     "color",                 1),
    (TYPE_UINT32,       1,     "_pad0",                 1),
]

VERTEX_COMPACT_STRUCT = [
    (TYPE_FLOAT32,      3,     "position",              1),
    (TYPE_UINT32 ,      1,     "normalPacked",          1),
]

# Must be careful with std140 offsets! They are set manually.
# Other structs are using std430 and padding is done automatically.
GLOBAL_UNIFORM_STRUCT = [
    (TYPE_FLOAT32,     44,      "view",                         1),
    (TYPE_FLOAT32,     44,      "invView",                      1),
    (TYPE_FLOAT32,     44,      "viewPrev",                     1),
    (TYPE_FLOAT32,     44,      "projection",                   1),
    (TYPE_FLOAT32,     44,      "invProjection",                1),
    (TYPE_FLOAT32,     44,      "projectionPrev",               1),

    (TYPE_FLOAT32,     44,      "volumeViewProj",               1),
    (TYPE_FLOAT32,     44,      "volumeViewProjInv",            1),
    (TYPE_FLOAT32,     44,      "volumeViewProj_Prev",          1),
    (TYPE_FLOAT32,     44,      "volumeViewProjInv_Prev",       1),

    (TYPE_FLOAT32,      1,      "cellWorldSize",                1),
    (TYPE_FLOAT32,      1,      "lightmapScreenCoverage",       1),
    (TYPE_UINT32,       1,      "illumVolumeEnable",            1),
    (TYPE_FLOAT32,      1,      "renderWidth",                  1),

    (TYPE_FLOAT32,      1,      "renderHeight",                 1),
    (TYPE_UINT32,       1,      "frameId",                      1),
    (TYPE_FLOAT32,      1,      "timeDelta",                    1),
    (TYPE_FLOAT32,      1,      "minLogLuminance",              1),

    (TYPE_FLOAT32,      1,      "maxLogLuminance",              1),
    (TYPE_FLOAT32,      1,      "luminanceWhitePoint",          1),
    (TYPE_UINT32,       1,      "stopEyeAdaptation",            1),
    (TYPE_UINT32,       1,      "directionalLightExists",       1),
    
    (TYPE_FLOAT32,      1,      "polyLightSpotlightFactor",     1),
    (TYPE_UINT32,       1,      "skyType",                      1),
    (TYPE_FLOAT32,      1,      "skyColorMultiplier",           1),
    (TYPE_UINT32,       1,      "skyCubemapIndex",              1),
    
    (TYPE_FLOAT32,      4,      "skyColorDefault",              1),

    (TYPE_FLOAT32,      4,      "cameraPosition",               1),
    (TYPE_FLOAT32,      4,      "cameraPositionPrev",           1),

    (TYPE_UINT32,       1,      "debugShowFlags",               1),
    (TYPE_UINT32,       1,      "indirSecondBounce",            1),
    (TYPE_UINT32,       1,      "lightCount",                   1),
    (TYPE_UINT32,       1,      "lightCountPrev",               1),

    (TYPE_FLOAT32,      1,      "emissionMapBoost",             1),
    (TYPE_FLOAT32,      1,      "emissionMaxScreenColor",       1),
    (TYPE_FLOAT32,      1,      "normalMapStrength",            1),
    (TYPE_FLOAT32,      1,      "skyColorSaturation",           1),

    (TYPE_UINT32,       1,      "maxBounceShadowsLights",           1),
    (TYPE_FLOAT32,      1,      "rayLength",                        1),
    (TYPE_UINT32,       1,      "rayCullBackFaces",                 1),
    (TYPE_UINT32,       1,      "rayCullMaskWorld",                 1),

    (TYPE_FLOAT32,      1,      "bloomIntensity",                   1),
    (TYPE_FLOAT32,      1,      "bloomThreshold",                   1),
    (TYPE_FLOAT32,      1,      "bloomEV",                          1),
    (TYPE_UINT32,       1,      "reflectRefractMaxDepth",           1),

    (TYPE_UINT32,       1,      "cameraMediaType",                  1),
    (TYPE_FLOAT32,      1,      "indexOfRefractionWater",           1),
    (TYPE_FLOAT32,      1,      "indexOfRefractionGlass",           1),
    (TYPE_FLOAT32,      1,      "waterTextureDerivativesMultiplier",1),

    (TYPE_UINT32,       1,      "volumeEnableType",                 1),
    (TYPE_FLOAT32,      1,      "volumeScattering",                 1),
    (TYPE_FLOAT32,      1,      "lensDirtIntensity",                1),
    (TYPE_UINT32,       1,      "waterNormalTextureIndex",          1),

    (TYPE_FLOAT32,      1,      "thinMediaWidth",                   1),
    (TYPE_FLOAT32,      1,      "time",                             1),
    (TYPE_FLOAT32,      1,      "waterWaveSpeed",                   1),
    (TYPE_FLOAT32,      1,      "waterWaveStrength",                1),

    (TYPE_FLOAT32,      4,      "waterColorAndDensity",             1),
    (TYPE_FLOAT32,      4,      "acidColorAndDensity",              1),

    (TYPE_FLOAT32,      1,      "cameraRayConeSpreadAngle",         1),
    (TYPE_FLOAT32,      1,      "waterTextureAreaScale",            1),
    (TYPE_UINT32,       1,      "dirtMaskTextureIndex",             1),
    (TYPE_FLOAT32,      1,      "upscaledRenderWidth",              1),

    (TYPE_FLOAT32,      4,      "worldUpVector",                    1),

    (TYPE_FLOAT32,      1,      "upscaledRenderHeight",             1),
    (TYPE_FLOAT32,      1,      "jitterX",                          1),
    (TYPE_FLOAT32,      1,      "jitterY",                          1),
    (TYPE_FLOAT32,      1,      "primaryRayMinDist",                1),

    (TYPE_UINT32,       1,      "rayCullMaskWorld_Shadow",          1),
    (TYPE_UINT32,       1,      "volumeAllowTintUnderwater",        1),
    # When set, ComposeNoisy reads A-SVGF temporal outputs instead of raw unfiltered
    (TYPE_UINT32,       1,      "rrTemporalPrefilterEnabled",       1),
    (TYPE_UINT32,       1,      "twirlPortalNormal",                1),

    (TYPE_UINT32,       1,      "lightIndexIgnoreFPVShadows",       1),
    (TYPE_FLOAT32,      1,      "gradientMultDiffuse",              1),
    (TYPE_FLOAT32,      1,      "gradientMultIndirect",             1),
    (TYPE_FLOAT32,      1,      "gradientMultSpecular",             1),

    (TYPE_FLOAT32,      1,      "minRoughness",                     1),
    (TYPE_FLOAT32,      1,      "volumeCameraNear",                 1),
    (TYPE_FLOAT32,      1,      "volumeCameraFar",                  1),
    (TYPE_UINT32,       1,      "antiFireflyEnabled",               1),

    (TYPE_FLOAT32,      4,      "volumeAmbient",                    1),
    (TYPE_FLOAT32,      4,      "volumeUnderwaterColor",            1),
    (TYPE_FLOAT32,      4,      "volumeFallbackSrcColor",           1),
    (TYPE_FLOAT32,      4,      "volumeFallbackSrcDirection",       1),

    (TYPE_FLOAT32,      1,      "volumeAsymmetry",                  1),
    (TYPE_UINT32,       1,      "volumeLightSourceIndex",           1),
    (TYPE_FLOAT32,      1,      "volumeFallbackSrcExists",          1),
    (TYPE_FLOAT32,      1,      "volumeLightMult",                  1),

    (TYPE_UINT32,       1,      "hdrDisplay",                       1),
    (TYPE_FLOAT32,      1,      "parallaxMaxDepth",                 1),
    (TYPE_UINT32,       1,      "fluidEnabled",                     1),
    # bit0=N, bit1=emis, bit2=metallic, bit3=height, bit4=roughness (Dev Materials A/B)
    (TYPE_UINT32,       1,      "materialStripFlags",               1),
    # Dev: mix authored roughness toward 1.0 (0=authored, 1=fully matte). Live A/B for RR shimmer.
    (TYPE_FLOAT32,      1,      "materialRoughnessTowardMatte",     1),
    # DLSS-RR disocclusion mask (transient-light history discard). Keep these 3
    # together with materialRoughnessTowardMatte so vec4 members stay 16-aligned.
    (TYPE_UINT32,       1,      "rrDisoccEnable",                   1),
    (TYPE_FLOAT32,      1,      "rrDisoccRatio",                    1),
    (TYPE_FLOAT32,      1,      "rrDisoccMinDelta",                 1),

    (TYPE_UINT32,       1,      "rrDisoccShowMask",                 1),
    # DLSS-RR firefly clamp, applied to the noisy lighting in CmNoisyCompose
    # before it reaches RR. RTGL's RR path has no spatial or temporal prefilter
    # at all (A-SVGF gets anti-firefly + variance-driven atrous), so single-
    # sample spikes go to NGX raw. Remix runs an equivalent clamp before RR.
    # 0 = off. Taken from the _pad slots so std140 layout is unchanged.
    (TYPE_FLOAT32,      1,      "rrFireflyThreshold",               1),
    (TYPE_FLOAT32,      1,      "rrFireflyMinLum",                  1),
    # ReSTIR reuse taps sampled with tiled blue noise instead of hash white
    # noise (the old "TODO: need low discrepancy noise" in selectLight_Direct).
    (TYPE_UINT32,       1,      "restirBlueNoise",                  1),

    (TYPE_FLOAT32,      4,      "fluidColor",                       1),

    # --- Samples per pixel + ReSTIR quality (Doom64-RT) -----------------------
    # The path tracer is 1 spp; convergence comes from temporal accumulation
    # (ReSTIR M + denoiser history), both of which motion legitimately destroys.
    # These trade GPU time for a quieter RAW signal, which helps A-SVGF and
    # DLSS-RR equally because it is upstream of both. All default to stock.
    # Two full vec4 groups so std140 alignment of what follows is unchanged.
    (TYPE_UINT32,       1,      "directSamples",                    1),
    (TYPE_UINT32,       1,      "indirectSamples",                  1),
    (TYPE_UINT32,       1,      "restirInitialSamples",             1),
    (TYPE_UINT32,       1,      "restirSpatialSamples",             1),

    (TYPE_FLOAT32,      1,      "restirSpatialRadius",              1),
    (TYPE_UINT32,       1,      "restirTemporalMCap",               1),
    # DLSS-RR albedo-guide floor. RR demodulates colour by these guides, so a
    # guide approaching zero makes that division explode -- correlated dark
    # filaments in dim, dark-albedo areas. The sky branch of CmNoisyCompose
    # already floors its guides ("bounded and non-zero", RR guide 3.4.2); the
    # world branch never did. 0 = no floor (old behaviour).
    (TYPE_FLOAT32,      1,      "rrGuideMin",                       1),
    # What the DLSS-RR albedo guides actually contain. RR does not merely divide
    # colour by these and multiply back -- it also uses them as edge-detection
    # and reprojection weights, so they must be SMOOTH material properties.
    #   0 = raw material: albedo / F0. What shipped before 2026-08-05, i.e. the
    #       iterations where the worm artifact was reportedly absent.
    #   1 = full demodulation: ro_d * throughput * ambient (2026-08-06 change).
    #       Correct in the "divide then remodulate" sense, but folds two
    #       non-smooth terms in: throughput is fetched in CHECKERBOARD space, so
    #       adjacent output pixels read non-adjacent texels; and
    #       getMaterialAmbient() is a thresholded quadratic that reaches exactly
    #       0 at black albedo, putting a derivative kink along dark-texture
    #       contours -- which no guide FLOOR can remove.
    #   2 = material demodulation only: ro_d / envBRDFApprox2, no throughput,
    #       no ambient. Separates "which factors" from "which reflectivity model".
    (TYPE_UINT32,       1,      "rrGuideMode",                      1),

    # Indirect ReSTIR temporal reuse is gated on framebufDISGradientHistory
    # (the A-SVGF antilag gradient): a tap is rejected when alpha > 0.25.
    # That buffer is written ONLY by CmASVGFGradientAtrous, which runs only
    # inside Denoiser::Denoise() -- and DLSS-RR skips Denoise() entirely in
    # favour of ComposeNoisy(). So under RR the gate reads a buffer nothing
    # updates. Either it is a dead no-op, or it rejects GI temporal reuse every
    # frame, which would leave indirect lighting effectively 1-spp with no
    # accumulation: surfaces fizzle, and only the denoiser's own history hides
    # it -- exactly what motion takes away.
    # 1 = apply the gate (stock), 0 = ignore it.
    (TYPE_UINT32,       1,      "restirIndirAntilag",               1),
    # Debug: write the shadow-ray visibility term into the unfiltered direct
    # buffer instead of radiance. Visibility is the ONLY thing a shadow ray
    # produces, so this separates "the occluder never blocks the ray" from
    # "the shadow is cast but drowned in fill light or smeared by the
    # denoiser" -- two causes that look identical in the final image, and that
    # four inconclusive A/B ladders failed to tell apart.
    # 1 = greyscale visibility (black = shadowed), 2 = red tint on shadowed
    # pixels over normal shading, so an umbra can be located in context.
    # Taken from a _pad slot so the std140 layout is unchanged: the group
    # ending at rrSpecHitDist must stay a whole vec4 or viewProjCubemap below
    # loses its 16-byte alignment and the C and GLSL layouts diverge.
    (TYPE_UINT32,       1,      "debugVisibility",                  1),
    (TYPE_UINT32,       1,      "_pad7",                            1),
    (TYPE_UINT32,       1,      "_pad8",                            1),

    # Shadow rays per pixel for DIRECT illumination. 1 = stock. The direct
    # estimate multiplies by a single binary traceVisibility(), so that 0/1 term
    # dominates the 1-spp variance and is untouched by anything ReSTIR does
    # (measured: blue-noise reuse taps changed nothing). Averaging N points on
    # the chosen light turns it into a soft fraction; variance falls ~1/sqrt(N).
    # Kept as a full vec4 group so viewProjCubemap below stays 16-byte aligned.
    (TYPE_UINT32,       1,      "shadowSamples",                    1),
    # Debug: write ReSTIR reservoir M (accumulated sample count) into the
    # unfiltered direct buffer instead of radiance. M is what makes 1-spp
    # ReSTIR converge; if it collapses under camera motion, that is the noise.
    (TYPE_UINT32,       1,      "debugRestirM",                     1),
    # ReSTIR temporal tap jitter radius, in pixels. Stock is 2. The jitter buys
    # decorrelation but costs history exactly where it is hardest to keep: on
    # grazing surfaces a 2px offset changes depth by far more than the flat 10%
    # reuse threshold, so the tap is rejected and M collapses. 0 = reproject
    # exactly (what RTXDI does).
    (TYPE_FLOAT32,      1,      "restirTemporalJitter",             1),
    # DLSS-RR: feed pInSpecularHitDistance (see the SpecularHitDistance framebuf).
    (TYPE_UINT32,       1,      "rrSpecHitDist",                    1),

    # --- Stylized water (Doom64-RT) ------------------------------------------
    # Doom 64 water flats (D64W2_01 / D64W1_01) are opaque FLOOR flats: the
    # physical refract+absorb path has nothing to refract into and reads far too
    # real for the art. See getStylizedWaterAlbedo() in RaygenPrimary.inl.
    # Two full scalar groups + one vec4 so viewProjCubemap stays 16-aligned.
    # 0 = off, stock physical water.
    (TYPE_FLOAT32,      1,      "stylizedWaterStrength",            1),
    # how hard the wave crests brighten the texture's own caustic veins
    (TYPE_FLOAT32,      1,      "stylizedWaterCaustic",             1),
    # Fresnel clamp: 1.0 = a true mirror at grazing angles
    (TYPE_FLOAT32,      1,      "stylizedWaterReflMax",             1),
    (TYPE_FLOAT32,      1,      "stylizedWaterRoughness",           1),

    # unlit on-screen sheen on the veins (casts no light)
    (TYPE_FLOAT32,      1,      "stylizedWaterGlow",                1),
    # luminance of the flat's brightest texel, used to normalize the vein mask
    (TYPE_FLOAT32,      1,      "stylizedWaterVeinRef",             1),
    # Diagnostic. 1 = paint every surface the shader sees as water:
    # MAGENTA if the stylized branch is taken, GREEN if RTGL flagged it water
    # but the stylized gate rejected it. No colour at all => the primitive never
    # got RG_MESH_PRIMITIVE_WATER, i.e. the JSON meta never reached it.
    (TYPE_FLOAT32,      1,      "stylizedWaterDebug",               1),
    # Reflection strength at NORMAL incidence. Real water is F0=0.02, i.e.
    # looking straight down the reflection is 2% and effectively invisible.
    # This is the artistic floor that makes it read as reflective from above.
    (TYPE_FLOAT32,      1,      "stylizedWaterReflMin",             1),

    # Body and crest colour per LIQUID, indexed by the 2-bit liquid id in the
    # geometry instance flags (GEOM_INST_FLAG_LIQUID_BIT*):
    #   0 water   1 nukage   2 sludge   3 blood
    # Doom 64's four liquids are the same animated 64-frame flat design in four
    # palettes, so they share this whole shader and differ only here. [0] is
    # water, which is the index a primitive with neither bit set gets.
    (TYPE_FLOAT32,      4,      "stylizedLiquidTint",               4),
    (TYPE_FLOAT32,      4,      "stylizedLiquidCrest",              4),

    # --- Lava (Doom64-RT) ----------------------------------------------------
    # The lava's emission cannot be baked bright enough to bloom: _e is 8-bit so
    # it caps at 1.0, screen emission is _e * emissionMaxScreenColor (3), and
    # rt_bloom_threshold is 16. lavaEmisBoost is applied to lava surfaces only,
    # which is the whole reason the geometry carries a LAVA flag.
    (TYPE_FLOAT32,      1,      "lavaEmisBoost",                    1),
    # Heat that moves. A low-frequency field drifting across the surface,
    # QUANTIZED to lavaFlowPixel world units so it stays as chunky as the flat
    # it sits on -- a smooth gradient over pixel art reads as a modern shader
    # bolted onto the wrong texture.
    (TYPE_FLOAT32,      1,      "lavaFlowStrength",                 1),
    (TYPE_FLOAT32,      1,      "lavaFlowSpeed",                    1),
    (TYPE_FLOAT32,      1,      "lavaFlowScale",                    1),
    (TYPE_FLOAT32,      1,      "lavaFlowPixel",                    1),
    # Whole-surface breathing, on top of the drift.
    (TYPE_FLOAT32,      1,      "lavaPulse",                        1),
    (TYPE_FLOAT32,      1,      "lavaPulseSpeed",                   1),
    # Indirect (GI) multiplier for lava emission, separate from the screen one.
    # This is the knob that lets the LAVA ITSELF light the room, as an area
    # source, instead of the grid of analytic point lights -- which is what a
    # lake actually is, and which does not leave circles of illumination on the
    # walls as the player walks past each one.
    (TYPE_FLOAT32,      1,      "lavaGiBoost",                      1),
    # Debug: 1 = paint every surface the shader sees as lava magenta. Answers
    # "does the LAVA flag survive into the shader" on its own, which no amount
    # of staring at brightness can.
    (TYPE_FLOAT32,      1,      "lavaDebug",                        1),
    # THE PAD COUNT IS LOAD-BEARING. This block is scalars in a std140 struct, so
    # it must be a multiple of FOUR floats or every field after it -- including
    # the mat4s at the end -- reads shifted, and the frame comes out black with
    # only the HUD on top. Eleven floats here did exactly that. Count them.
    (TYPE_FLOAT32,      1,      "_padlava0",                        1),
    (TYPE_FLOAT32,      1,      "_padlava1",                        1),
    (TYPE_FLOAT32,      1,      "_padlava",                         1),
    # Hue of the heat, multiplying the lava's emission on BOTH the screen and
    # the GI path. The flat's own cracks photograph yellow-orange once they are
    # boosted; pulling green and blue down is what makes it read as molten rock
    # rather than as a light bulb. A vec4, so it does not disturb the scalar
    # count above -- which is load-bearing, see the note there.
    (TYPE_FLOAT32,      4,      "lavaTint",                         1),

    # --- Directional light: sky-reach test (Doom64-RT) ------------------------
    # A shadow ray that hits NOTHING is scored as lit (RtMissShadowCheck.rmiss
    # sets isShadowed = 0). Doom maps are not watertight and have no geometry
    # above a ceiling, so rays leave the world through T-junctions at wall/
    # ceiling seams and come back "lit" -- the moon then washes rooms that have
    # no opening at all. Widening the light or gating apertures cannot fix that,
    # because nothing is being squeezed through anything.
    #
    # The test: in a Doom map the sky is the ONLY legitimate way out, so a ray
    # that escapes without hitting sky geometry escaped through a modelling gap.
    # 1 = require the ray to reach sky (WORLD_2) before counting as lit.
    (TYPE_FLOAT32,      1,      "sunRequireSky",                    1),
    # Diagnostic. 1 = ignore the light's colour and paint by WHY a surface is
    # lit: RED  = the ray reached sky, i.e. genuine moonlight through a real
    # opening; GREEN = the ray escaped into the void, i.e. a leak. Surfaces the
    # moon does not reach are untouched. Works whether or not sunRequireSky is
    # on, so the leak can be seen before it is fixed.
    (TYPE_FLOAT32,      1,      "sunLeakDebug",                     1),
    # Brightness of those debug colours. These rooms are near-black, so the
    # classification is invisible at the moon's real intensity.
    (TYPE_FLOAT32,      1,      "sunLeakDebugMul",                  1),
    # Max length of the extra sky probe ray, in meters. Only traced when the
    # normal shadow ray already missed, i.e. only for pixels that are currently
    # lit, so this is bounded by how much of the screen the moon touches.
    (TYPE_FLOAT32,      1,      "sunSkyProbeMaxDist",               1),

    # --- Projected water caustics (Doom64-RT) --------------------------------
    # Caustics cast BY the water ONTO the geometry around it. A path tracer at
    # 1 spp will never find these by itself (they are a focused specular-to-
    # diffuse path), so they are projected: each shading point fires one probe
    # ray straight down, and if it lands on a water-flagged surface within
    # waterCausticDist, its lighting is modulated by an animated caustic field
    # sampled from the same water normal texture the surface waves use.
    # Multiplicative, so a dark room stays dark. 0 = off, no probe ray traced.
    (TYPE_FLOAT32,      1,      "waterCausticGain",                 1),
    # caustic field frequency, in UV per world unit
    (TYPE_FLOAT32,      1,      "waterCausticScale",                1),
    (TYPE_FLOAT32,      1,      "waterCausticSpeed",                1),
    # how far below a surface the water may be and still light it (world units)
    (TYPE_FLOAT32,      1,      "waterCausticDist",                 1),

    # How far ABOVE the water the caustics still reach, metres. Separate from
    # waterCausticDist on purpose: real caustics climb only a little way up a
    # wall, while the probe itself has to reach much further sideways to clear
    # a pool's ledge. One combined range made the pattern run up the full
    # height of every wall.
    (TYPE_FLOAT32,      1,      "waterCausticRise",                 1),
    # how far the probe tilts along the surface normal (0 = straight down)
    (TYPE_FLOAT32,      1,      "waterCausticSlant",                1),
    # Extra gain for VERTICAL receivers. Caustics on a wall are seen at a
    # grazing angle and are physically fainter than the same pattern on a floor,
    # so a single gain that reads well on the pool bottom leaves the walls
    # barely visible -- and raising it brightens everything instead.
    (TYPE_FLOAT32,      1,      "waterCausticWallBoost",            1),
    # --- Illuminated fog (Doom64-RT) -----------------------------------------
    # The froxel pass scatters ONE light: whatever TryGetVolumetricLight picked,
    # which on a map with no sun and no RG_LIGHT_ADDITIONAL_VOLUMETRIC light is
    # nothing at all -- so its fog is flat ambient with no source in it. 1 makes
    # each froxel run the full direct-lighting estimate instead, so every light
    # in the map scatters. Taken from the _padc1 slot, so std140 is unchanged.
    (TYPE_UINT32,       1,      "volumeAllLights",                  1),

    # Scattering albedo of the medium. Multiplies the whole in-scattered term --
    # ambient AND lit -- so a coloured fog colours what glows inside it too.
    # Extinction stays monochrome (the transmittance channel is a single float
    # all the way to CmPrepareFinal), so distance fades TOWARD this colour
    # rather than filtering by it. { 1, 1, 1 } is the no-op.
    #
    # NEAR value of a near->far ramp: the froxel grid's slices are uniform in
    # DISTANCE (VOLUMETRIC_DISTANCE_POW 1), so cell.z is a straight depth
    # fraction and near/far can be separated for free. Setting the _Far pair
    # equal to these is the uniform medium.
    (TYPE_FLOAT32,      4,      "volumeMediaColor",                 1),
    (TYPE_FLOAT32,      4,      "volumeMediaColorFar",              1),

    # Density and tint at the FAR plane of the volume, and the shape of the
    # interpolation between them (1 = linear, >1 holds the near value longer and
    # thickens late, <1 thickens immediately).
    (TYPE_FLOAT32,      1,      "volumeScatteringFar",              1),
    (TYPE_FLOAT32,      1,      "volumeDensityCurve",               1),
    # Fog only: in-scattering is faded out within this many metres OF A LIGHT,
    # so a light carried at the camera (flashlight, muzzle flash) does not white
    # out the froxels in front of it by inverse square. Keyed off the LIGHT's
    # distance, not the camera's, so the beam further down the corridor -- the
    # look this is all for -- survives. 0 = physical behaviour.
    (TYPE_FLOAT32,      1,      "volumeLightNearFade",              1),
    (TYPE_FLOAT32,      1,      "_padf2",                           1),

    # --- Localised smoke (Doom64-RT) -----------------------------------------
    # A separate group, appended AFTER the whole volume block rather than
    # interleaved with it, so a mistake here cannot silently shift
    # volumeMediaColor's offset and retune the shipped fog.
    #
    # One full scalar group of four, then two vec4 arrays, so viewProjCubemap
    # below keeps its 16-byte alignment.
    #
    # smokeCount 0 is the no-smoke state: the loop in Smoke.h does not execute
    # and the medium arithmetic collapses back to the fog's exactly.
    (TYPE_UINT32,       1,      "smokeCount",                       1),
    # Near-light fade, and all-lights temporal blend, used ONLY in froxels that
    # contain smoke -- chosen per cell, so fog cells keep volumeLightNearFade
    # and the stock 0.05. A muzzle flash lighting the smoke at the barrel is the
    # whole effect, and the fog's 2 m fade plus a 0.05 blend would erase it and
    # then smear what was left over ~0.7 s.
    (TYPE_FLOAT32,      1,      "smokeLightNearFade",               1),
    (TYPE_FLOAT32,      1,      "smokeIllumBlend",                  1),
    # Whether a froxel CONTAINING smoke runs the all-lights estimate. Separate
    # from volumeAllLights on purpose: that one switches the WHOLE volume off the
    # single-light path, and the single-light path is the only place the sun's
    # sky-probe test lives -- so flipping it per frame deletes a map's light
    # shafts for as long as a puff exists. This is read per cell instead.
    (TYPE_UINT32,       1,      "smokeAllLights",                   1),

    # Shader-side probe (rt_smoke_debug 2/3). 2 paints the froxels a puff
    # actually covers; 3 paints EVERY froxel whenever the puff list is non-empty.
    # The pair separates "the shader cannot read the uniform" from "it reads it
    # and no cell passes the sphere test", which no amount of engine-side logging
    # can distinguish.
    (TYPE_UINT32,       1,      "smokeDebug",                       1),
    # Metres beyond which a light stops lighting SMOKE. Only smoke cells consult
    # it, so fog and the global medium keep receiving every light as before.
    (TYPE_FLOAT32,      1,      "smokeLightFarFade",                1),
    # Spatial blur applied to the froxel volume before it is integrated, 0..1 as
    # a blend against the raw grid. The volume is lit at ONE sample per cell and
    # is the only buffer in the renderer with no spatial filter of any kind, so
    # its variance reaches the screen untouched. Volumetric lighting is
    # low-frequency by nature -- blurring it costs almost nothing visually.
    (TYPE_FLOAT32,      1,      "volumeSpatialBlur",                1),
    # ONE pad, not two. The scalar run from smokeCount to here must be a
    # MULTIPLE OF FOUR: C packs scalars contiguously, std140 aligns the vec4
    # arrays that follow to 16 bytes, and any other count makes the two layouts
    # disagree from that point on. Adding volumeSpatialBlur without removing a
    # pad took the run to nine, shifted every array by 12 bytes in GLSL only,
    # and produced smoke that vanished and black bands on screen.
    # Ceiling on the in-scattered radiance inside SMOKE. A carried light -- the
    # flashlight, the plasma rifle's glow -- sits at ~0 m, so it lights the
    # froxels around it by inverse square and a dense puff in front of the camera
    # goes pure white. That is the same physics rt_fog_light_near exists for, and
    # smoke deliberately disables that fade so a muzzle flash can light its own
    # puff. This is the backstop: it bounds the result instead of the cause.
    (TYPE_FLOAT32,      1,      "smokeMaxLight",                    1),

    # Samples per froxel INSIDE SMOKE. The volume is estimated at one NEE sample
    # and one shadow ray per cell, and that is the source of the variance every
    # filter here is trying to hide. Raising it globally would be unaffordable --
    # 900k cells -- but a puff occupies a few per cent of them, so paying for
    # more samples only where smoke is costs almost nothing and attacks the noise
    # where it is made rather than smoothing it afterwards.
    #
    # A full group of four: the scalar run before the vec4 arrays must stay
    # divisible by four or C and std140 disagree. See tools/check_uniform_layout.py.
    (TYPE_UINT32,       1,      "smokeSpp",                         1),
    # How far, in FROXELS, the per-pixel volume sample is jittered. Stock is 2,
    # which hides the grid in a low-contrast medium -- but with dense smoke the
    # jitter reaches across geometry silhouettes into columns belonging to other
    # surfaces, and that draws dark outlines around everything seen through the
    # smoke.
    (TYPE_FLOAT32,      1,      "volumeDither",                     1),
    # Whether SCREEN EMISSION is attenuated by the volumetric medium. It was
    # added after the volumetric composite and outside its guard, so an emissive
    # panel shone through fog and smoke at full strength as though nothing were
    # in front of it.
    (TYPE_UINT32,       1,      "volumeOccludeEmis",                1),

    # PIXEL-ART STYLIZATION, smoke only (Doom64-RT). The froxel volume produces
    # a smooth exponential falloff, which is physically right and reads as an
    # airbrushed blob against art that is entirely hard-edged pixels. These
    # posterize the puff's falloff into bands and optionally snap its evaluation
    # to a world grid, so smoke gains the stepped edge the sprites have.
    #
    # It lives INSIDE smoke_evalAt rather than as a screen-space filter for the
    # reason every other smoke change does: with smokeCount 0 that function
    # returns zero whatever these say, so the fog's arithmetic still collapses
    # bit for bit and ab.cmd smoke-fogsafe still holds.
    #
    # 0 = the smooth falloff, 1 = fully banded.
    (TYPE_FLOAT32,      1,      "smokeStylize",                     1),
    # How many bands. Few is chunky and poster-like, many approaches smooth.
    (TYPE_UINT32,       1,      "smokeStylizeSteps",                1),
    # Metres of world-space voxel snapping, 0 = off. WORLD space, not screen:
    # screen-space blocks crawl as the camera turns, which reads as noise rather
    # than as style. World voxels stay put in the level.
    (TYPE_FLOAT32,      1,      "smokeStylizeGrid",                 1),

    # TWO pads now, not one, and the count is arithmetic rather than taste. The
    # scalar run from smokeCount to here must stay a MULTIPLE OF FOUR, so what
    # is ADDED has to be a multiple of four too: three new fields above + one
    # new pad = four. The first attempt added three fields and three pads, which
    # is six, and check_uniform_layout.py caught every vec4 array after it
    # sitting 8 bytes further along in GLSL than in C -- exactly the failure that
    # note below describes, found by the build instead of by a day of debugging.
    # SMOKE'S OWN UNLIT FLOOR, and it needs one because the volume's ambient is
    # per FRAME and global. rt_smoke_ambient was only ever applied when smoke
    # OWNED the volume (no fog and rt_volume_type 0), which the shipping config
    # never is -- so smoke had no self-visibility at all and was visible only
    # while a light was actually on it. A muzzle flash lasts 2-3 frames; the
    # trail it leaves lasts two seconds, and all of that was invisible.
    #
    # Per froxel, like smokeLightNearFade and smokeIllumBlend, so a cell with no
    # smoke in it is untouched and the fog collapse still holds.
    (TYPE_FLOAT32,      1,      "smokeAmbient",                     1),
    # How hard a smoke cell asserts its OWN albedo against the medium it is
    # mixed into. smoke_blendTint is a density-weighted average, which is
    # correct and has a bad consequence: a thin puff standing in a lit room is
    # mostly the ROOM's medium colour, so powder smoke turns beige in a beige
    # room and vanishes. 0 = the honest weighted average, 1 = the puff keeps its
    # own colour regardless of how thin it is.
    (TYPE_FLOAT32,      1,      "smokeTintBias",                    1),

    # ABSORPTION, smoke only, and the reason smoke is invisible in a bright room.
    #
    # The froxel stores rgb = in-scattered light and a = EXTINCTION, which is
    # scattering + absorbtion -- and absorbtion has been hardcoded 0 since the
    # volume was written, so the medium is purely scattering. A purely
    # scattering medium ADDS as much light as it takes away: in a dark room the
    # addition is what you see, and in a room lit evenly from above the two
    # cancel and the puff has no contrast against the wall in either direction.
    # That is exactly the report -- a barrel's fat burst still reads because its
    # density is 8x higher, while the gun's thin wisp disappears entirely.
    #
    # Real powder smoke is sooty: its single-scattering albedo is well under 1.
    # This is that missing fraction, and it is what lets smoke read DARK against
    # a bright background instead of only bright against a dark one.
    (TYPE_FLOAT32,      1,      "smokeAbsorb",                      1),
    # A brightness multiplier for the light scattered INSIDE smoke only.
    #
    # The volume is single-scattering: a wall receives full GI and a puff
    # receives exactly one bounce, so a puff in a lit room is always darker than
    # its surroundings no matter how much light reaches it. Real smoke is pale
    # because of MULTIPLE scattering, which this has no way to compute. This is
    # the cheap stand-in for it, and it exists per froxel because volumeLightMult
    # belongs to the fog on nine tuned maps.
    (TYPE_FLOAT32,      1,      "smokeLightMult",                   1),
    (TYPE_UINT32,       1,      "_pads8",                           1),
    (TYPE_UINT32,       1,      "_pads9",                           1),

    # xyz = centre in world space (metres, the same space as a light's position
    # and as volume_getCenter's output), w = radius in metres.
    (TYPE_FLOAT32,      4,      "smokePuffs",           CONST[ "SMOKE_PUFF_MAX" ]),
    # rgb = scattering albedo, a = density at the core, already scaled by the
    # volume's slice thickness engine-side so it reads as optical depth per
    # METRE and does not change meaning when the volume's reach does.
    (TYPE_FLOAT32,      4,      "smokeAlbedoDensity",   CONST[ "SMOKE_PUFF_MAX" ]),
    # x = the puff's radius ACROSS THE VIEW, in metres, which is the only radius
    # you actually see. smokePuffs.w is its radius ALONG the view, held at half a
    # froxel slice so the grid can resolve it at all. yzw spare.
    (TYPE_FLOAT32,      4,      "smokeShape",           CONST[ "SMOKE_PUFF_MAX" ]),

    # for std140
    (TYPE_FLOAT32,     44,      "viewProjCubemap",              6),
    (TYPE_FLOAT32,     44,      "skyCubemapRotationTransform",  1),
]

GEOM_INSTANCE_STRUCT = [
    (TYPE_FLOAT32,      4,      "model_0",              1),
    (TYPE_FLOAT32,      4,      "model_1",              1),
    (TYPE_FLOAT32,      4,      "model_2",              1),
    (TYPE_FLOAT32,      4,      "prevModel_0",          1),
    (TYPE_FLOAT32,      4,      "prevModel_1",          1),
    (TYPE_FLOAT32,      4,      "prevModel_2",          1),

    (TYPE_UINT32,       1,      "flags",                1),
    (TYPE_UINT32,       1,      "texture_base",         1),
    (TYPE_UINT32,       1,      "texture_base_ORM",     1),
    (TYPE_UINT32,       1,      "texture_base_N",       1),
    (TYPE_UINT32,       1,      "texture_base_E",       1),
    (TYPE_UINT32,       1,      "texture_layer1",       1),
    (TYPE_UINT32,       1,      "texture_layer2",       1),
    (TYPE_UINT32,       1,      "texture_layer3",       1),

    (TYPE_UINT32,       1,      "colorFactor_base",     1),
    (TYPE_UINT32,       1,      "colorFactor_layer1",   1),
    (TYPE_UINT32,       1,      "colorFactor_layer2",   1),
    (TYPE_UINT32,       1,      "colorFactor_layer3",   1),

    (TYPE_UINT32,       1,      "baseVertexIndex",      1),
    (TYPE_UINT32,       1,      "baseIndexIndex",       1),
    (TYPE_UINT32,       1,      "prevBaseVertexIndex",  1),
    (TYPE_UINT32,       1,      "prevBaseIndexIndex",   1),

    (TYPE_UINT32,       1,      "vertexCount",          1),
    (TYPE_UINT32,       1,      "indexCount",           1),
    (TYPE_UINT32,       1,      "texture_base_D",       1),
    (TYPE_UINT32,       1,      "roughnessDefault_metallicDefault", 1),

    (TYPE_FLOAT32,      1,      "emissiveMult",         1),
    (TYPE_UINT32,       1,      "firstVertex_Layer1",   1),
    (TYPE_UINT32,       1,      "firstVertex_Layer2",   1),
    (TYPE_UINT32,       1,      "firstVertex_Layer3",   1),
]

# TODO: make more compact
LIGHT_ENCODED_STRUCT = [
    (TYPE_UINT32,       1,      "lightType",            1),
    (TYPE_UINT32,       1,      "colorE5",              1),
    (TYPE_FLOAT32,      1,      "ldata0",               1),
    (TYPE_FLOAT32,      1,      "ldata1",               1),
    (TYPE_FLOAT32,      1,      "ldata2",               1),
    (TYPE_FLOAT32,      1,      "ldata3",               1),
]

# TODO: light index / target pdf - 16 bits
LIGHT_IN_CELL = [
    (TYPE_UINT32,       1,      "selected_lightIndex",  1),
    (TYPE_FLOAT32,      1,      "selected_targetPdf",   1),
    (TYPE_FLOAT32,      1,      "weightSum",            1),
]

TONEMAPPING_STRUCT = [
    (TYPE_UINT32,       1,      "histogram",            CONST["COMPUTE_LUM_HISTOGRAM_BIN_COUNT"]),
    (TYPE_FLOAT32,      1,      "avgLuminance",         1),
]

INDIRECT_DRAW_CMD_STRUCT = [
    (TYPE_UINT32,       1,      "indexCount",           1),
    (TYPE_UINT32,       1,      "instanceCount",        1),
    (TYPE_UINT32,       1,      "firstIndex",           1),
    (TYPE_INT32,        1,      "vertexOffset",         1),
    (TYPE_UINT32,       1,      "firstInstance",        1),
    (TYPE_FLOAT32,      1,      "positionToCheck_X",    1),
    (TYPE_FLOAT32,      1,      "positionToCheck_Y",    1),
    (TYPE_FLOAT32,      1,      "positionToCheck_Z",    1),
]

LENS_FLARES_INSTANCE_STRUCT = [
    (TYPE_UINT32,       1,      "packedColor",          1),
    (TYPE_UINT32,       1,      "textureIndex",         1),
    (TYPE_UINT32,       1,      "emissiveTextureIndex", 1),
    (TYPE_FLOAT32,      1,      "emissiveMult",         1),
]

PORTAL_INSTANCE_STRUCT = [
    (TYPE_FLOAT32,      4,      "inPosition",               1),
    (TYPE_FLOAT32,      4,      "outPosition",              1),
    (TYPE_FLOAT32,      4,      "outDirection",             1),
    (TYPE_FLOAT32,      4,      "outUp",                    1),
]

STRUCT_ALIGNMENT_NONE       = 0
STRUCT_ALIGNMENT_STD430     = 1
STRUCT_ALIGNMENT_STD140     = 2

STRUCT_BREAK_TYPE_NONE      = 0
STRUCT_BREAK_TYPE_COMPLEX   = 1
STRUCT_BREAK_TYPE_ONLY_C    = 2

# (structTypeName): (structDefinition, onlyForGLSL, alignmentFlags, breakComplex)
# alignmentFlags    -- if using a struct in dynamic array, it must be aligned with 16 bytes
# breakType         -- if member's type is not primitive and its count>0 then
#                      it'll be represented as an array of primitive types
STRUCTS = {
    "ShVertex":                 (VERTEX_STRUCT,                 False,  STRUCT_ALIGNMENT_STD140,    0),
    "ShVertexCompact":          (VERTEX_COMPACT_STRUCT,         False,  STRUCT_ALIGNMENT_STD140,    0),
    "ShGlobalUniform":          (GLOBAL_UNIFORM_STRUCT,         False,  STRUCT_ALIGNMENT_STD140,    STRUCT_BREAK_TYPE_ONLY_C),
    "ShGeometryInstance":       (GEOM_INSTANCE_STRUCT,          False,  STRUCT_ALIGNMENT_STD430,    0),
    "ShTonemapping":            (TONEMAPPING_STRUCT,            False,  0,                          0),
    "ShLightEncoded":           (LIGHT_ENCODED_STRUCT,          False,  0,                          0),
    "ShLightInCell":            (LIGHT_IN_CELL,                 False,  STRUCT_ALIGNMENT_STD430,    0),
    "ShIndirectDrawCommand":    (INDIRECT_DRAW_CMD_STRUCT,      False,  STRUCT_ALIGNMENT_STD430,    0),
    # TODO: should be STRUCT_ALIGNMENT_STD430, but current generator is not great as it just adds pads at the end, so it's 0
    "ShLensFlareInstance":      (LENS_FLARES_INSTANCE_STRUCT,   False,  0,                          0),
    "ShPortalInstance":         (PORTAL_INSTANCE_STRUCT,        False,  STRUCT_ALIGNMENT_STD140,    0),
}

# --------------------------------------------------------------------------------------------- #
# User defined buffers: uniform, storage buffer
# --------------------------------------------------------------------------------------------- #





# --------------------------------------------------------------------------------------------- #
# User defined framebuffers
# --------------------------------------------------------------------------------------------- #

FRAMEBUF_DESC_SET_NAME              = "DESC_SET_FRAMEBUFFERS"
FRAMEBUF_BASE_BINDING               = 0
FRAMEBUF_PREFIX                     = "framebuf"
FRAMEBUF_SAMPLER_POSTFIX            = "_Sampler"
FRAMEBUF_DEBUG_NAME_PREFIX          = "Framebuf "
FRAMEBUF_STORE_PREV_POSTFIX         = "_Prev"
FRAMEBUF_SAMPLER_INVALID_BINDING    = "FB_SAMPLER_INVALID_BINDING"

# only info for 2 frames are used: current and previous
FRAMEBUF_FLAGS_STORE_PREV           = 1 << 0
FRAMEBUF_FLAGS_NO_SAMPLER           = 1 << 1
FRAMEBUF_FLAGS_IS_ATTACHMENT        = 1 << 2
FRAMEBUF_FLAGS_FORCE_SIZE_BLOOM     = 1 << 3
FRAMEBUF_FLAGS_FORCE_SIZE_1_3       = 1 << 4
FRAMEBUF_FLAGS_BILINEAR_SAMPLER     = 1 << 9
FRAMEBUF_FLAGS_UPSCALED_SIZE        = 1 << 10
FRAMEBUF_FLAGS_SINGLE_PIXEL_SIZE    = 1 << 11
FRAMEBUF_FLAGS_USAGE_TRANSFER       = 1 << 12

# only these flags are shown for C++ side
FRAMEBUF_FLAGS_ENUM = {
    "FRAMEBUF_FLAGS_IS_ATTACHMENT"      : FRAMEBUF_FLAGS_IS_ATTACHMENT,
    "FRAMEBUF_FLAGS_FORCE_SIZE_BLOOM"   : FRAMEBUF_FLAGS_FORCE_SIZE_BLOOM,
    "FRAMEBUF_FLAGS_FORCE_SIZE_1_3"     : FRAMEBUF_FLAGS_FORCE_SIZE_1_3,
    "FRAMEBUF_FLAGS_BILINEAR_SAMPLER"   : FRAMEBUF_FLAGS_BILINEAR_SAMPLER,
    "FRAMEBUF_FLAGS_UPSCALED_SIZE"      : FRAMEBUF_FLAGS_UPSCALED_SIZE,
    "FRAMEBUF_FLAGS_SINGLE_PIXEL_SIZE"  : FRAMEBUF_FLAGS_SINGLE_PIXEL_SIZE,
    "FRAMEBUF_FLAGS_USAGE_TRANSFER"     : FRAMEBUF_FLAGS_USAGE_TRANSFER,
}

FRAMEBUFFERS = {
    # (image name)                      : (base format type, components,    flags)
    "Albedo"                            : (TYPE_PACK_11,    COMPONENT_RGB,  FRAMEBUF_FLAGS_IS_ATTACHMENT),
    "IsSky"                             : (TYPE_UINT8,      COMPONENT_R,    0),
    "Normal"                            : (TYPE_UINT32,     COMPONENT_R,    FRAMEBUF_FLAGS_STORE_PREV),
    "MetallicRoughness"                 : (TYPE_UNORM8,     COMPONENT_RG,   FRAMEBUF_FLAGS_STORE_PREV),
    "DepthWorld"                        : (TYPE_FLOAT16,    COMPONENT_R,    FRAMEBUF_FLAGS_STORE_PREV),
    "DepthGrad"                         : (TYPE_FLOAT16,    COMPONENT_R,    0),
    "DepthNdc"                          : (TYPE_FLOAT32,    COMPONENT_R,    0),
    "DepthFluid"                        : (TYPE_FLOAT32,    COMPONENT_R,    0),
    "DepthFluidTemp"                    : (TYPE_FLOAT32,    COMPONENT_R,    0),
    "FluidNormal"                       : (TYPE_UINT32,     COMPONENT_R,    FRAMEBUF_FLAGS_IS_ATTACHMENT),
    "FluidNormalTemp"                   : (TYPE_UINT32,     COMPONENT_R,    0),
    "Motion"                            : (TYPE_FLOAT16,    COMPONENT_RGBA, 0),
    "UnfilteredDirect"                  : (TYPE_PACK_E5,    COMPONENT_RGB,  0),
    "UnfilteredSpecular"                : (TYPE_PACK_E5,    COMPONENT_RGB,  0),
    "UnfilteredIndir"                   : (TYPE_PACK_E5,    COMPONENT_RGB,  0),
    "SurfacePosition"                   : (TYPE_FLOAT32,    COMPONENT_RGBA, FRAMEBUF_FLAGS_STORE_PREV),
    "VisibilityBuffer"                  : (TYPE_FLOAT32,    COMPONENT_RGBA, FRAMEBUF_FLAGS_STORE_PREV),
    "ViewDirection"                     : (TYPE_FLOAT16,    COMPONENT_RGBA, FRAMEBUF_FLAGS_STORE_PREV),
    "PrimaryToReflRefr"                 : (TYPE_UINT32,     COMPONENT_RGBA, 0),
    "Throughput"                        : (TYPE_FLOAT16,    COMPONENT_RGBA, 0),
    "PreFinal"                          : (TYPE_FLOAT16,    COMPONENT_RGBA, 0),
    "Final"                             : (TYPE_FLOAT16,    COMPONENT_RGBA, FRAMEBUF_FLAGS_IS_ATTACHMENT),

    "UpscaledPing"                      : (TYPE_FLOAT16,    COMPONENT_RGBA, FRAMEBUF_FLAGS_IS_ATTACHMENT | FRAMEBUF_FLAGS_UPSCALED_SIZE | FRAMEBUF_FLAGS_USAGE_TRANSFER),  # dst for DLSS and blitting in,
    "UpscaledPong"                      : (TYPE_FLOAT16,    COMPONENT_RGBA, FRAMEBUF_FLAGS_IS_ATTACHMENT | FRAMEBUF_FLAGS_UPSCALED_SIZE | FRAMEBUF_FLAGS_USAGE_TRANSFER),  #       src for WipeEffectSource 

    # for upscalers
    "MotionDlss"                        : (TYPE_FLOAT16,    COMPONENT_RG,   0),
    "Reactivity"                        : (TYPE_UNORM8,     COMPONENT_R,    FRAMEBUF_FLAGS_IS_ATTACHMENT),
    "HudOnly"                           : (TYPE_UNORM8,     COMPONENT_RGBA, FRAMEBUF_FLAGS_IS_ATTACHMENT | FRAMEBUF_FLAGS_UPSCALED_SIZE | FRAMEBUF_FLAGS_USAGE_TRANSFER),  # src for framegen

    "AccumHistoryLength"                : (TYPE_FLOAT16,    COMPONENT_RGBA, FRAMEBUF_FLAGS_STORE_PREV),
    
    # TODO: pack float16 to e5
    "DiffTemporary"                     : (TYPE_PACK_E5,    COMPONENT_RGB,  0),
    "DiffAccumColor"                    : (TYPE_PACK_E5,    COMPONENT_RGB,  FRAMEBUF_FLAGS_STORE_PREV),
    "DiffAccumMoments"                  : (TYPE_FLOAT16,    COMPONENT_RG,   FRAMEBUF_FLAGS_STORE_PREV),
    "DiffColorHistory"                  : (TYPE_FLOAT16,    COMPONENT_RGBA, 0),
    "DiffPingColorAndVariance"          : (TYPE_FLOAT16,    COMPONENT_RGBA, 0),
    "DiffPongColorAndVariance"          : (TYPE_FLOAT16,    COMPONENT_RGBA, 0),
    
    "SpecAccumColor"                    : (TYPE_PACK_E5,    COMPONENT_RGB,  FRAMEBUF_FLAGS_STORE_PREV),
    "SpecPingColor"                     : (TYPE_PACK_E5,    COMPONENT_RGB,  0),
    "SpecPongColor"                     : (TYPE_PACK_E5,    COMPONENT_RGB,  0),
    
    "IndirAccum"                        : (TYPE_PACK_E5,    COMPONENT_RGB,  FRAMEBUF_FLAGS_STORE_PREV),
    "IndirPing"                         : (TYPE_PACK_E5,    COMPONENT_RGB,  0),
    "IndirPong"                         : (TYPE_PACK_E5,    COMPONENT_RGB,  0),

    "AtrousFilteredVariance"            : (TYPE_FLOAT16,    COMPONENT_R,    0),
    
    "NormalDecal"                       : (TYPE_UINT32,     COMPONENT_R,    FRAMEBUF_FLAGS_IS_ATTACHMENT),
    
    "Scattering"                        : (TYPE_FLOAT16,    COMPONENT_RGBA, FRAMEBUF_FLAGS_STORE_PREV),
    "ScatteringHistory"                 : (TYPE_FLOAT16,    COMPONENT_R,    FRAMEBUF_FLAGS_STORE_PREV),
    
    # need separate one for RT, to resolve checkerboarded across multiple pixels
    "ScreenEmisRT"                      : (TYPE_PACK_11,    COMPONENT_RGB,  0),
    "ScreenEmission"                    : (TYPE_PACK_11,    COMPONENT_RGB,  FRAMEBUF_FLAGS_IS_ATTACHMENT),

    "Bloom"                             : (TYPE_FLOAT16,    COMPONENT_RGBA, FRAMEBUF_FLAGS_FORCE_SIZE_BLOOM | FRAMEBUF_FLAGS_BILINEAR_SAMPLER | FRAMEBUF_FLAGS_UPSCALED_SIZE),
    "Bloom_Mip1"                        : (TYPE_FLOAT16,    COMPONENT_RGBA, FRAMEBUF_FLAGS_FORCE_SIZE_BLOOM | FRAMEBUF_FLAGS_BILINEAR_SAMPLER | FRAMEBUF_FLAGS_UPSCALED_SIZE),
    "Bloom_Mip2"                        : (TYPE_FLOAT16,    COMPONENT_RGBA, FRAMEBUF_FLAGS_FORCE_SIZE_BLOOM | FRAMEBUF_FLAGS_BILINEAR_SAMPLER | FRAMEBUF_FLAGS_UPSCALED_SIZE),
    "Bloom_Mip3"                        : (TYPE_FLOAT16,    COMPONENT_RGBA, FRAMEBUF_FLAGS_FORCE_SIZE_BLOOM | FRAMEBUF_FLAGS_BILINEAR_SAMPLER | FRAMEBUF_FLAGS_UPSCALED_SIZE),
    "Bloom_Mip4"                        : (TYPE_FLOAT16,    COMPONENT_RGBA, FRAMEBUF_FLAGS_FORCE_SIZE_BLOOM | FRAMEBUF_FLAGS_BILINEAR_SAMPLER | FRAMEBUF_FLAGS_UPSCALED_SIZE),
    "Bloom_Mip5"                        : (TYPE_FLOAT16,    COMPONENT_RGBA, FRAMEBUF_FLAGS_FORCE_SIZE_BLOOM | FRAMEBUF_FLAGS_BILINEAR_SAMPLER | FRAMEBUF_FLAGS_UPSCALED_SIZE),
    "Bloom_Mip6"                        : (TYPE_FLOAT16,    COMPONENT_RGBA, FRAMEBUF_FLAGS_FORCE_SIZE_BLOOM | FRAMEBUF_FLAGS_BILINEAR_SAMPLER | FRAMEBUF_FLAGS_UPSCALED_SIZE),
    "Bloom_Mip7"                        : (TYPE_FLOAT16,    COMPONENT_RGBA, FRAMEBUF_FLAGS_FORCE_SIZE_BLOOM | FRAMEBUF_FLAGS_BILINEAR_SAMPLER | FRAMEBUF_FLAGS_UPSCALED_SIZE),
  
    "WipeEffectSource"                  : (TYPE_FLOAT16,    COMPONENT_RGBA, FRAMEBUF_FLAGS_UPSCALED_SIZE | FRAMEBUF_FLAGS_USAGE_TRANSFER), # dst to copy in
    
    "Reservoirs"                        : (TYPE_UINT32,     COMPONENT_RG,   FRAMEBUF_FLAGS_STORE_PREV),
    "ReservoirsInitial"                 : (TYPE_UINT32,     COMPONENT_RG,   0),

    "IndirectReservoirsInitial"         : (TYPE_UINT32,     COMPONENT_RGBA, 0),

    # DLSS-RR: pInDisocclusionMask (sentinel 10000.0 forces RR history discard)
    # and the tile-luminance history it is computed from. Written by CmNoisyCompose
    # in regular (non-checkerboard) pixel space, RR path only.
    "RrDisocclusion"                    : (TYPE_FLOAT16,    COMPONENT_R,    0),
    "RrLumHistory"                      : (TYPE_FLOAT16,    COMPONENT_R,    FRAMEBUF_FLAGS_STORE_PREV),

    # DLSS-RR: pInSpecularHitDistance -- world distance from the shading point to
    # whatever produced the highlight. RR needs this to reproject specular, which
    # does NOT live on the surface; without it specular is reprojected as if it
    # did, so glossy surfaces smear and fizzle under camera motion. Was bound to
    # FB_DEPTH_WORLD once (primary-hit camera distance -- the wrong signal) and
    # then set to nullptr; this is the correct value. Written by RtRaygenDirect
    # in checkerboard space, resolved like the other direct outputs.
    "SpecularHitDistance"               : (TYPE_FLOAT16,    COMPONENT_R,    0),
}

if GRADIENT_ESTIMATION_ENABLED:
    FRAMEBUFFERS.update({
        "GradientInputs"                : (TYPE_FLOAT16,    COMPONENT_RG,   FRAMEBUF_FLAGS_STORE_PREV),
        "DISPingGradient"               : (TYPE_UNORM8,     COMPONENT_RGBA, FRAMEBUF_FLAGS_FORCE_SIZE_1_3),
        "DISPongGradient"               : (TYPE_UNORM8,     COMPONENT_RGBA, FRAMEBUF_FLAGS_FORCE_SIZE_1_3),
        "DISGradientHistory"            : (TYPE_UNORM8,     COMPONENT_RGBA, FRAMEBUF_FLAGS_FORCE_SIZE_1_3),
        "GradientPrevPix"               : (TYPE_UINT8,      COMPONENT_R,    FRAMEBUF_FLAGS_FORCE_SIZE_1_3),
    })


# ---
# User defined structs END
# ---








def getAllConstDefs(constDict):
    return "\n".join([
        "#define %s (%s)" % (name, str(value))
        for name, value in constDict.items()
    ]) + "\n\n"


def getMemberSizeStd430(baseType, dim, count):
    if dim == 1:
        return GLSL_TYPE_SIZES_STD_430[baseType] * count
    elif count == 1:
        return GLSL_TYPE_SIZES_STD_430[(baseType, dim)]
    else:
        return GLSL_TYPE_SIZES_STD_430[baseType] * dim * count


def getMemberActualSize(baseType, dim, count):
    if dim == 1:
        return TYPE_ACTUAL_SIZES[baseType] * count
    else:
        return TYPE_ACTUAL_SIZES[(baseType, dim)] * count

CURRENT_PAD_INDEX = 0

def getPadsForStruct(typeNames, uint32ToAdd):
    global CURRENT_PAD_INDEX
    r = ""
    padStr = "    " + typeNames[TYPE_UINT32] + " __pad%d;\n"
    for i in range(uint32ToAdd):
        r += padStr % (CURRENT_PAD_INDEX + i)
    CURRENT_PAD_INDEX += uint32ToAdd
    return r


# useVecMatTypes:
def getStruct(name, definition, typeNames, alignmentType, breakType):
    r = "struct " + name + "\n{\n"

    if alignmentType == STRUCT_ALIGNMENT_STD140 and typeNames == C_TYPE_NAMES:
        print("Struct \"" + name + "\" is using std140, alignment must be set manually.")

    global CURRENT_PAD_INDEX
    CURRENT_PAD_INDEX = 0

    curSize = 0
    curOffset = 0

    for baseType, dim, mname, count in definition:
        assert(count > 0)
        r += "    "

        if count == 1:
            if dim == 1:
                r +="%s %s" % (typeNames[baseType], mname)
            elif (baseType, dim) in typeNames:
                r += "%s %s" % (typeNames[(baseType, dim)], mname)
            elif dim <= 4:
                r += "%s %s[%d]" % (typeNames[baseType], mname, dim)
            else:
                if USE_MULTIDIMENSIONAL_ARRAYS_IN_C:
                    r += "%s %s[%d][%d]" % (typeNames[baseType], mname, dim // 10, dim % 10)
                else:
                    r += "%s %s[%d]" % (typeNames[baseType], mname, (dim // 10) * (dim % 10))
        else:
            #if dim > 4 and typeNames == C_TYPE_NAMES:
            #    raise Exception("If count > 1, dimensions must be in [1..4]")
            if breakType == STRUCT_BREAK_TYPE_COMPLEX or (typeNames == C_TYPE_NAMES and breakType == STRUCT_BREAK_TYPE_ONLY_C):
                if dim <= 4:
                    r += "%s %s[%d]" % (typeNames[baseType], mname, align4(count * dim))
                else:
                    r += "%s %s[%d]" % (typeNames[baseType], mname, align4(count * int(TYPE_ACTUAL_SIZES[(baseType, dim)] / 4)))
            else:
                if dim == 1:
                    r += "%s %s[%d]" % (typeNames[baseType], mname, count)
                elif (baseType, dim) in typeNames:
                    r += "%s %s[%d]" % (typeNames[(baseType, dim)], mname, count)
                else:
                    r += "%s %s[%d][%d]" % (typeNames[baseType], mname, count, dim)

        r += ";\n"

        if (alignmentType == STRUCT_ALIGNMENT_STD430):
            for i in range(count):
                memberSize = getMemberActualSize(baseType, dim, 1)
                memberAlignment = getMemberSizeStd430(baseType, dim, 1)

                alignedOffset = align(curOffset, memberAlignment)
                diff = alignedOffset - curOffset

                if diff > 0:
                    assert (diff) % 4 == 0
                    r += getPadsForStruct(typeNames, diff // 4)

                curSize += curOffset + memberSize

    if (alignmentType == STRUCT_ALIGNMENT_STD430) and curSize % 16 != 0:
        if (curSize % 16) % 4 != 0:
            raise Exception("Size of struct %s is not 4-byte aligned!" % name)
        uint32ToAdd = (align(curSize, 16) - curSize) // 4
        r += getPadsForStruct(typeNames, uint32ToAdd)

    r += "};\n"
    return r


def getAllStructDefs(typeNames):
    return "\n".join(
        getStruct(name, structDef, typeNames, alignmentType, breakType)
        for name, (structDef, onlyForGLSL, alignmentType, breakType) in STRUCTS.items()
        if not (onlyForGLSL and (typeNames == C_TYPE_NAMES))
    ) + "\n"


def capitalizeFirstLetter(s):
    return s[:1].upper() + s[1:]



CURRENT_FRAMEBUF_BINDING_COUNT = 0

def getGLSLFramebufDeclaration(name, baseFormat, components, flags):
    global CURRENT_FRAMEBUF_BINDING_COUNT

    binding = FRAMEBUF_BASE_BINDING + CURRENT_FRAMEBUF_BINDING_COUNT
    bindingSampler = binding + 1
    CURRENT_FRAMEBUF_BINDING_COUNT += 1

    r = ""
    if flags & FRAMEBUF_FLAGS_IS_ATTACHMENT:
        r += "#ifndef " + FRAMEBUF_IGNORE_ATTACHMENTS_DEFINE + "\n"

    template = ("layout(set = %s, binding = %d, %s) uniform %s %s;")

    r += template % (FRAMEBUF_DESC_SET_NAME, binding, 
        GLSL_IMAGE_FORMATS[(baseFormat, components)], 
        GLSL_IMAGE_2D_TYPE[baseFormat], name)

    if flags & FRAMEBUF_FLAGS_STORE_PREV:
        r += "\n"
        r += getGLSLFramebufDeclaration(
            name + FRAMEBUF_STORE_PREV_POSTFIX, baseFormat, components, 
            flags & ~FRAMEBUF_FLAGS_STORE_PREV)

    if flags & FRAMEBUF_FLAGS_IS_ATTACHMENT:
        r += "\n#endif"

    return r


def getGLSLFramebufSamplerDeclaration(name, baseFormat, components, flags):
    global CURRENT_FRAMEBUF_BINDING_COUNT

    binding = FRAMEBUF_BASE_BINDING + CURRENT_FRAMEBUF_BINDING_COUNT - 1
    bindingSampler = binding + 1
    CURRENT_FRAMEBUF_BINDING_COUNT += 1

    r = ""
    if flags & FRAMEBUF_FLAGS_IS_ATTACHMENT:
        r += "#ifndef " + FRAMEBUF_IGNORE_ATTACHMENTS_DEFINE + "\n"

    templateSampler = ("layout(set = %s, binding = %d) uniform %s %s;")

    r += templateSampler % (FRAMEBUF_DESC_SET_NAME, bindingSampler,
        GLSL_SAMPLER_2D_TYPE[baseFormat], name + FRAMEBUF_SAMPLER_POSTFIX)

    if flags & FRAMEBUF_FLAGS_STORE_PREV:
        r += "\n"
        r += getGLSLFramebufSamplerDeclaration(
            name + FRAMEBUF_STORE_PREV_POSTFIX, baseFormat, components, 
            flags & ~FRAMEBUF_FLAGS_STORE_PREV)

    if flags & FRAMEBUF_FLAGS_IS_ATTACHMENT:
        r += "\n#endif"

    return r


def getGLSLFramebufPackUnpackE5(name, withPrev):
    templateImgStore = ("void imageStore%s(const ivec2 pix, const vec3 unpacked) "
                        "{ imageStore(%s, pix, uvec4(encodeE5B9G9R9(unpacked))); }")
    templateTxlFetch = ("vec3 texelFetch%s(const ivec2 pix)"
                        "{ return decodeE5B9G9R9(texelFetch(%s, pix, 0).r); }")
    r  = templateImgStore % (name, FRAMEBUF_PREFIX + name) + "\n"
    r += templateTxlFetch % (name, FRAMEBUF_PREFIX + name + FRAMEBUF_SAMPLER_POSTFIX) + "\n"
    if withPrev:
        r += templateTxlFetch % (name + FRAMEBUF_STORE_PREV_POSTFIX, FRAMEBUF_PREFIX + name + FRAMEBUF_STORE_PREV_POSTFIX + FRAMEBUF_SAMPLER_POSTFIX) + "\n"
    return r
    

def getAllGLSLFramebufDeclarations():
    global CURRENT_FRAMEBUF_BINDING_COUNT
    CURRENT_FRAMEBUF_BINDING_COUNT = 0
    return "#ifdef " + FRAMEBUF_DESC_SET_NAME \
        \
        + "\n\n// framebuffer indices\n" \
        \
        + "\n".join(
        "#define FB_IMAGE_INDEX_%s %d" % (s, d) for (s, d) in getAllFramebufEnumTuples()
        ) \
        \
        + "\n\n// framebuffers\n" \
        \
        + "\n".join(
            getGLSLFramebufDeclaration(FRAMEBUF_PREFIX + name, baseFormat, components, flags)
            for name, (baseFormat, components, flags) in FRAMEBUFFERS.items()
        ) \
        \
        + "\n\n// samplers\n" \
        + "\n".join(
            getGLSLFramebufSamplerDeclaration(FRAMEBUF_PREFIX + name, baseFormat, components, flags)
            for name, (baseFormat, components, flags) in FRAMEBUFFERS.items()
            if not (flags & FRAMEBUF_FLAGS_NO_SAMPLER)
        ) \
        \
        + "\n\n// pack/unpack formats\n" \
        + "\n".join(
            getGLSLFramebufPackUnpackE5(name, flags & FRAMEBUF_FLAGS_STORE_PREV)
            for name, (baseFormat, components, flags) in FRAMEBUFFERS.items()
            if baseFormat == TYPE_PACK_E5 and not (flags & FRAMEBUF_FLAGS_NO_SAMPLER)
        ) \
        \
        + "\n\n#endif\n"


def removeCoupledDuplicateChars(str, charToRemove = '_'):
    r = ""
    for i in range(0, len(str)):
        if i == 0 or str[i] != str[i - 1] or str[i] != charToRemove:
            r += str[i]
    return r


# make all letters capital and insert "_" before 
# capital letters in the original string
def capitalizeForEnum(s):
    return removeCoupledDuplicateChars("_".join(filter(None, re.split("([A-Z][^A-Z]*)", s))).upper())


# returns (name, index) tuples for framebuf-s
def getAllFramebufEnumTuples():
    names = []
    for name, (_, _, flags) in FRAMEBUFFERS.items():
        names.append(name)
        if flags & FRAMEBUF_FLAGS_STORE_PREV:
            names.append(name + FRAMEBUF_STORE_PREV_POSTFIX)

    return [(capitalizeForEnum(names[i]), i) for i in range(len(names))]


def getAllFramebufConstants():
    fbConst = "#define " + FRAMEBUF_SAMPLER_INVALID_BINDING + " 0xFFFFFFFF\n\n"

    fbEnum = "enum FramebufferImageIndex\n{\n" + "\n".join(
        "    FB_IMAGE_INDEX_%s = %d," % (s, d) for (s, d) in getAllFramebufEnumTuples()
    ) + "\n};\n\n"

    fbFlags = "enum FramebufferImageFlagBits\n{\n" + "\n".join(
        "    FB_IMAGE_FLAGS_%s = %d," % (flName, flValue)
        for (flName, flValue) in FRAMEBUF_FLAGS_ENUM.items()
    ) + "\n};\ntypedef uint32_t FramebufferImageFlags;\n\n"

    return fbConst + fbEnum + fbFlags


def getPublicFlags(flags):
    r = " | ".join(
        "RTGL1::FB_IMAGE_FLAGS_" + flName
        for (flName, flValue) in FRAMEBUF_FLAGS_ENUM.items()
        if flValue & flags
    )
    if r == "":
        return "0"
    else:
        return r


_ShFramebuffers_Count = 0


def getAllVulkanFramebufDeclarations():
    return ("constexpr uint32_t ShFramebuffers_Count = %s;\n"
            "extern const VkFormat ShFramebuffers_Formats[];\n"
            "extern const FramebufferImageFlags ShFramebuffers_Flags[];\n"
            "extern const uint32_t ShFramebuffers_Bindings[];\n"
            "extern const uint32_t ShFramebuffers_BindingsSwapped[];\n"
            "extern const uint32_t ShFramebuffers_Sampler_Bindings[];\n"
            "extern const uint32_t ShFramebuffers_Sampler_BindingsSwapped[];\n"
            "extern const char *const ShFramebuffers_DebugNames[];\n"
            "extern const wchar_t *const ShFramebuffers_DebugNamesW[];\n\n") % str(_ShFramebuffers_Count)


def getAllVulkanFramebufDefinitions():
    template = ("const VkFormat RTGL1::ShFramebuffers_Formats[] = \n{\n%s};\n\n"
                "const RTGL1::FramebufferImageFlags RTGL1::ShFramebuffers_Flags[] = \n{\n%s};\n\n"
                "const uint32_t RTGL1::ShFramebuffers_Bindings[] = \n{\n%s};\n\n"
                "const uint32_t RTGL1::ShFramebuffers_BindingsSwapped[] = \n{\n%s};\n\n"
                "const uint32_t RTGL1::ShFramebuffers_Sampler_Bindings[] = \n{\n%s};\n\n"
                "const uint32_t RTGL1::ShFramebuffers_Sampler_BindingsSwapped[] = \n{\n%s};\n\n"
                "const char *const RTGL1::ShFramebuffers_DebugNames[] = \n{\n%s};\n\n"
                "const wchar_t *const RTGL1::ShFramebuffers_DebugNamesW[] = \n{\n%s};\n\n")
    TAB_STR = "    "
    formats = ""
    count = 0
    publicFlags = ""
    samplerCount = 0
    bindings = ""    
    bindingsSwapped = ""
    samplerBindings = ""
    samplerBindingsSwapped = ""
    names = ""
    for name, (baseFormat, components, flags) in FRAMEBUFFERS.items():
        formats += TAB_STR + VULKAN_IMAGE_FORMATS[(baseFormat, components)] + ", // " + name + "\n"
        names += TAB_STR + "\"" + FRAMEBUF_DEBUG_NAME_PREFIX + name + "\",\n"
        publicFlags += TAB_STR + getPublicFlags(flags) + ", // " + name + "\n"

        if not flags & FRAMEBUF_FLAGS_STORE_PREV:
            bindings                += TAB_STR + str(count)         + ",\n"
            bindingsSwapped         += TAB_STR + str(count)         + ",\n"
        else:
            bindings                += TAB_STR + str(count)         + ",\n"
            bindings                += TAB_STR + str(count + 1)     + ",\n"
            bindingsSwapped         += TAB_STR + str(count + 1)     + ",\n"
            bindingsSwapped         += TAB_STR + str(count)         + ",\n"
            
            formats += TAB_STR + VULKAN_IMAGE_FORMATS[(baseFormat, components)] + ", // " + name + FRAMEBUF_STORE_PREV_POSTFIX + "\n"
            names += TAB_STR + "\"" + FRAMEBUF_DEBUG_NAME_PREFIX + name + FRAMEBUF_STORE_PREV_POSTFIX + "\",\n"
            publicFlags += TAB_STR + getPublicFlags(flags) + ", // " + name + FRAMEBUF_STORE_PREV_POSTFIX + "\n"
            count += 1

        count += 1

    for name, (baseFormat, components, flags) in FRAMEBUFFERS.items():
        bindingIndex     = str(count + samplerCount)
        bindingIndexNext = str(count + samplerCount + 1)
        
        if flags & FRAMEBUF_FLAGS_NO_SAMPLER:
            bindingIndex = bindingIndexNext = FRAMEBUF_SAMPLER_INVALID_BINDING

        if not flags & FRAMEBUF_FLAGS_STORE_PREV:
            samplerBindings         += TAB_STR + bindingIndex       + ",\n"
            samplerBindingsSwapped  += TAB_STR + bindingIndex       + ",\n"
        else:
            samplerBindings         += TAB_STR + bindingIndex       + ",\n"
            samplerBindings         += TAB_STR + bindingIndexNext   + ",\n"
            samplerBindingsSwapped  += TAB_STR + bindingIndexNext   + ",\n"
            samplerBindingsSwapped  += TAB_STR + bindingIndex       + ",\n"
            samplerCount += 1

        samplerCount += 1

    wnames = names.replace(f"\"{FRAMEBUF_DEBUG_NAME_PREFIX}", f"L\"{FRAMEBUF_DEBUG_NAME_PREFIX}")

    global _ShFramebuffers_Count
    _ShFramebuffers_Count = count

    return template % (formats, publicFlags, bindings, bindingsSwapped, samplerBindings, samplerBindingsSwapped, names, wnames)


FILE_HEADER = "// This file was generated by GenerateShaderCommon.py\n\n"


def writeToC(commonHeaderFile, fbHeaderFile, fbSourceFile):
    commonHeaderFile.write(FILE_HEADER)
    commonHeaderFile.write("#pragma once\n\n")
    commonHeaderFile.write("namespace RTGL1\n{\n\n")
    commonHeaderFile.write("#include <stdint.h>\n\n")
    commonHeaderFile.write(getAllConstDefs(CONST))
    commonHeaderFile.write(getAllStructDefs(C_TYPE_NAMES))
    commonHeaderFile.write("}")

    fbSourceFile.write(FILE_HEADER)
    fbSourceFile.write("#include \"%s\"\n\n" % os.path.basename(fbHeaderFile.name))
    fbSourceFile.write(getAllVulkanFramebufDefinitions())

    fbHeaderFile.write(FILE_HEADER)
    fbHeaderFile.write("#pragma once\n\n")
    fbHeaderFile.write("#include \"../Common.h\"\n\n")
    fbHeaderFile.write("namespace RTGL1\n{\n\n")
    fbHeaderFile.write(getAllFramebufConstants())
    fbHeaderFile.write(getAllVulkanFramebufDeclarations())
    fbHeaderFile.write("}")


def writeToGLSL(f):
    f.write(FILE_HEADER)
    f.write(getAllConstDefs(CONST))
    f.write(getAllConstDefs(CONST_GLSL_ONLY))
    f.write(getAllStructDefs(GLSL_TYPE_NAMES))
    f.write(getAllGLSLFramebufDeclarations())


def main():
    basePath = ""

    for i in range(len(sys.argv)):
        if "--help" == sys.argv[i] or "-help" == sys.argv[i]:
            print("--path     : specify path to target folder in the next argument")
            return
        if "--path" == sys.argv[i]:
            if i + 1 < len(sys.argv):
                basePath = sys.argv[i + 1]
                if not os.path.exists(basePath):
                    print("Folder with path \"" + basePath + "\" doesn't exist.")
                    return
            else:
                print("--path expects folder path in the next argument.")
                return

    evalConst()
    # with open('ShaderConfig.csv', newline='') as csvfile:
    with open(basePath + "ShaderCommonC.h", "w") as commonHeaderFile:
        with open(basePath + "ShaderCommonCFramebuf.h", "w") as fbHeaderFile:
            with open(basePath + "ShaderCommonCFramebuf.cpp", "w") as fbSourceFile:
                writeToC(commonHeaderFile, fbHeaderFile, fbSourceFile)
    with open(basePath + "ShaderCommonGLSL.h", "w") as f:
        writeToGLSL(f)

# main
if __name__ == "__main__":
    main()
