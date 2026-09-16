/* ACO shader test: real GLSL -> SPIR-V (via glslangValidator, offline)
 * fed into bc250_aco_compile_spirv(); validates the ICD shader-path
 * plumbing (sizes, S_ENDPGM presence, register counts).
 * NOTE: wrapper currently emits placeholder ISA; real ACO codegen
 * needs a Mesa ACO build (meson+LLVM) - tracked as future work. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "..\\src\\vulkan\\bc250_aco_wrapper.h"

static int fails = 0;
#define CHECK(c, msg) do { fprintf(stderr, "%-24s : %s\n", msg, (c) ? "OK" : "FAIL"); if (!(c)) fails++; } while (0)

static uint32_t *readfile(const char *path, long *outBytes)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t *b = (uint32_t *)malloc(n ? n : 1);
    if (b && n) { if (fread(b, 1, n, f) != (size_t)n) { free(b); b = NULL; n = 0; } }
    fclose(f);
    *outBytes = n;
    return b;
}

static int test_one(const char *spvPath, BC250_ShaderType type, const char *name)
{
    long n = 0;
    uint32_t *spv = readfile(spvPath, &n);
    CHECK(spv && n > 0, name);
    if (!spv) return 1;
    CHECK(*(uint32_t *)spv == 0x07230203, "spirv magic");
    fprintf(stderr, "   spv bytes=%ld words=%ld\n", n, n / 4);

    BC250_GpuInfo gi = bc250_aco_get_default_gpu_info();
    CHECK(gi.gfxLevel == 1013 && gi.waveSize == 32, "gpu info gfx1013");

    BC250_ShaderResult r = bc250_aco_compile_spirv(spv, (uint32_t)(n / 4), type, &gi);
    CHECK(r.success, "compile success");
    CHECK(r.code && r.codeSize == 8, "code 8 dwords");
    /* S_ENDPGM = 0x7E in SOPP opcode field: our stub emits (0x7E<<16)|0 */
    CHECK(r.code && (r.code[0] & 0xFFFF0000u) == 0x007E0000u, "S_ENDPGM present");
    CHECK(r.numVGPRs <= 256 && r.numSGPRs <= 128, "reg counts sane");
    fprintf(stderr, "   sgpr=%u vgpr=%u lds=%u scratch=%u\n",
        r.numSGPRs, r.numVGPRs, r.ldsSize, r.scratchBytes);
    bc250_aco_free_shader(&r);
    free(spv);
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    CHECK(bc250_aco_init(), "aco init");
    test_one("..\\output\\aco-shader-test.vert.spv", BC250_SHADER_VERTEX, "vert spv");
    test_one("..\\output\\aco-shader-test.frag.spv", BC250_SHADER_FRAGMENT, "frag spv");
    fprintf(stderr, fails ? "RESULT: %d FAILURES\n" : "RESULT: ALL PASS\n", fails);
    return fails ? 1 : 0;
}
