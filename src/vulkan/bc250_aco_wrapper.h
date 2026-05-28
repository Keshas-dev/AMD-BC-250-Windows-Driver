/*
 * BC-250 ACO Shader Compiler Wrapper
 * 
 * Simplified interface to Mesa ACO compiler for BC-250 GPU.
 * Compiles SPIR-V/GLSL shaders to GFX10 ISA (GCN binary).
 *
 * Based on Mesa ACO (MIT license)
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef BC250_ACO_WRAPPER_H
#define BC250_ACO_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shader types */
typedef enum {
    BC250_SHADER_VERTEX   = 0,
    BC250_SHADER_FRAGMENT = 1,
    BC250_SHADER_COMPUTE  = 2,
    BC250_SHADER_GEOMETRY = 3,
    BC250_SHADER_HULL     = 4,
    BC250_SHADER_DOMAIN   = 5,
} BC250_ShaderType;

/* Shader compilation result */
typedef struct {
    uint32_t*   code;           /* GPU binary code (DWORDs) */
    uint32_t    codeSize;       /* Code size in DWORDs */
    uint32_t    numSGPRs;       /* Scalar GPRs used */
    uint32_t    numVGPRs;       /* Vector GPRs used */
    uint32_t    numSharedVGPRs; /* Shared VGPRs (GFX10) */
    uint32_t    ldsSize;        /* LDS size in bytes */
    uint32_t    scratchBytes;   /* Scratch memory per wave */
    bool        success;        /* Compilation success */
    char        error[256];     /* Error message if failed */
} BC250_ShaderResult;

/* GPU info for compilation */
typedef struct {
    uint32_t    familyId;       /* GPU family (GFX10) */
    uint32_t    gfxLevel;       /* GFX level (1013 for BC-250) */
    uint32_t    numCUs;         /* Compute units */
    uint32_t    waveSize;       /* Wave size (32 for RDNA2) */
    uint32_t    vgprsPerCU;     /* VGPRs per CU */
    uint32_t    sgprsPerCU;     /* SGPRs per CU */
} BC250_GpuInfo;

/* Initialize ACO compiler for BC-250 */
bool bc250_aco_init(void);

/* Compile a shader from SPIR-V binary */
BC250_ShaderResult bc250_aco_compile_spirv(
    const uint32_t* spirvCode,
    uint32_t spirvSize,
    BC250_ShaderType shaderType,
    const BC250_GpuInfo* gpuInfo
);

/* Compile a shader from GLSL source (placeholder) */
BC250_ShaderResult bc250_aco_compile_glsl(
    const char* glslSource,
    BC250_ShaderType shaderType,
    const BC250_GpuInfo* gpuInfo
);

/* Free compiled shader */
void bc250_aco_free_shader(BC250_ShaderResult* result);

/* Get default GPU info for BC-250 */
BC250_GpuInfo bc250_aco_get_default_gpu_info(void);

#ifdef __cplusplus
}
#endif

#endif /* BC250_ACO_WRAPPER_H */
