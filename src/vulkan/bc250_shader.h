/*
 * BC-250 Shader Compiler - Header
 * SPIR-V → GFX10 ISA compilation
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once
#ifndef BC250_SHADER_H
#define BC250_SHADER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SHADER_VERTEX   = 0,
    SHADER_FRAGMENT = 1,
    SHADER_COMPUTE  = 2,
    SHADER_GEOMETRY = 3,
} ShaderType;

typedef struct {
    uint32_t*   code;
    uint32_t    codeSize;
    uint32_t    numSGPRs;
    uint32_t    numVGPRs;
    uint32_t    ldsSize;
    bool        success;
    char        error[256];
} CompiledShader;

/* Compile SPIR-V to GFX10 ISA */
CompiledShader bc250_compile_spirv(const uint32_t* spirv, uint32_t size, ShaderType type);

/* Free compiled shader */
void bc250_free_shader(CompiledShader* shader);

/* Get GPU info for BC-250 */
void bc250_get_gpu_info(uint32_t* familyId, uint32_t* gfxLevel, uint32_t* waveSize);

#ifdef __cplusplus
}
#endif

#endif /* BC250_SHADER_H */
