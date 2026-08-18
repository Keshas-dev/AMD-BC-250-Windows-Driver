# BC-250 Linux Kernel Patches — Išsami Analizė

**Data:** 2026-08-07  
**Autorius:** AI Assistant  
**Statusas:** Research / Documentation

---

## 1. Santrauka

BC-250 (Cyan Skillfish / GFX1013) yra PS5 Oberon APU su 40 CU iš kurių tik 24 aktyvios. Linux kernel reikalauja specifinių patch'ų kad:
1. Pripažintų BC-250 GPU (PCI IDs, IP offsets)
2. Inicializuotų GPU be IP discovery lentelės
3. Leistų 40 CU unlock per kernel parameter
4. Leistų SMU valdymą ir telemetriją

---

## 2. Oficialūs AMD/Deucher Patch'ai (2025-08)

Šie patch'ai patekti į kernel 6.12+ per Alex Deucher (AMD).

### 2.1 Patch 1: PCI IDs
```
drm/amd: add more cyan skillfish PCI ids
```
- Prideda `PCI\VEN_1002&DEV_13FE` ir kitus BC-250 variantus

### 2.2 Patch 2: IP Offset Support
```
drm/amdgpu: add ip offset support for cyan skillfish
```
**Failas:** `cyan_skillfish_reg_init.c`

BC-250 neturi IP discovery lentelės VRAM'e, todėl visi GC/HDP/NBIO/MP0/MP1 offset'ai turi būti nurodyti rankiniu būdu:

```c
int cyan_skillfish_reg_base_init(struct amdgpu_device *adev) {
    uint32_t i;
    adev->gfx.xcc_mask = 1;
    for (i = 0; i < MAX_INSTANCE; ++i) {
        adev->reg_offset[GC_HWIP][i] = (uint32_t*)(&(GC_BASE.instance[i]));
        // GC_BASE = {0x1260, 0xA000, 0x02402C00}
        adev->reg_offset[HDP_HWIP][i] = (uint32_t*)(&(HDP_BASE.instance[i]));
        adev->reg_offset[MMHUB_HWIP][i] = (uint32_t*)(&(MMHUB_BASE.instance[i]));
        adev->reg_offset[ATHUB_HWIP][i] = (uint32_t*)(&(ATHUB_BASE.instance[i]));
        adev->reg_offset[NBIO_HWIP][i] = (uint32_t*)(&(NBIO_BASE.instance[i]));
        adev->reg_offset[MP0_HWIP][i] = (uint32_t*)(&(MP0_BASE.instance[i]));
        adev->reg_offset[MP1_HWIP][i] = (uint32_t*)(&(MP1_BASE.instance[i]));
        adev->reg_offset[VCN_HWIP][i] = (uint32_t*)(&(UVD0_BASE.instance[i]));
        adev->reg_offset[DF_HWIP][i] = (uint32_t*)(&(DF_BASE.instance[i]));
        adev->reg_offset[DCE_HWIP][i] = (uint32_t*)(&(DMU_BASE.instance[i]));
        adev->reg_offset[OSSSYS_HWIP][i] = (uint32_t*)(&(OSSSYS_BASE.instance[i]));
        adev->reg_offset[SDMA0_HWIP][i] = (uint32_t*)(&(GC_BASE.instance[i]));
        adev->reg_offset[SDMA1_HWIP][i] = (uint32_t*)(&(GC_BASE.instance[i]));
        adev->reg_offset[SMUIO_HWIP][i] = (uint32_t*)(&(SMUIO_BASE.instance[i]));
        adev->reg_offset[THM_HWIP][i] = (uint32_t*)(&(THM_BASE.instance[i]));
        adev->reg_offset[CLK_HWIP][i] = (uint32_t*)(&(CLK_BASE.instance[i]));
    }
    return 0;
}
```

### 2.3 Patch 3: Be IP Discovery
```
drm/amdgpu: add support for cyan skillfish without IP discovery
```
Inizijavimas kai `ip_discovery` firmware nėra.

### 2.4 Patch 4: GPU Info Firmware
```
drm/amdgpu: add support for cyan skillfish gpu_info
```
```c
MODULE_FIRMWARE("amdgpu/cyan_skillfish_gpu_info.bin");
// ...
case CHIP_CYAN_SKILLFISH:
    chip_name = "cyan_skillfish";
    break;
```

### 2.5 Patch 5: SMU Išjungimas
```
drm/amdgpu: don't enable SMU on cyan skillfish
```
BC-250 SMU veikia kitu būdu (per SOS, ne per AMDGPI).

---

## 3. APU Liberation Patch'ai (arieltune, cachenetics)

12 patch'ų serija BC-250 "išsivadavimui" (kernel 7.0.9).

### 3.1 Patch'ų Lentelė

| Patch | Paskirtis | Kodas |
|---|---|---|
| 01-03, 07-08 | SMU race-free path | Papildomi SMU_MSG IDs, SCLK_MAX 2500MHz, `amdgpu_smu_send_raw` debugfs |
| 04-05, 11 | Telemetry | PMFW reporting, clocks, temp, pstates, voltages |
| 09 | CPU clock limits | `cclk_soft_min/max` debugfs |
| **12** | **40 CU UNLOCK** | `bc250_cc_write_mode`, SPI_PG + CC + RLC masks |

### 3.2 Patch 12: 40 CU Unlock (kritinis)

```c
// gfx_v10_0.c → gfx_v10_0_get_cu_info()
static int bc250_cc_write_mode;
module_param(bc250_cc_write_mode, int, 0444);
#define BC250_PCI_DEVICE_ID 0x13FE

// gfx_v10_0_get_cu_info() viduje:
mutex_lock(&adev->grbm_idx_mutex);
if (bc250_cc_write_mode > 0 && adev->pdev->device == BC250_PCI_DEVICE_ID) {
    int bc_se, bc_sh;
    for (bc_se = 0; bc_se < adev->gfx.config.max_shader_engines; bc_se++) {
        for (bc_sh = 0; bc_sh < adev->gfx.config.max_sh_per_se; bc_sh++) {
            gfx_v10_0_select_se_sh(adev, bc_se, bc_sh, 0xffffffff, 0);
            WREG32_SOC15(GC, 0, mmCC_GC_SHADER_ARRAY_CONFIG, 0);
            WREG32_SOC15(GC, 0, mmSPI_PG_ENABLE_STATIC_WGP_MASK, 0x1f);
            WREG32_SOC15(GC, 0, mmRLC_PG_ALWAYS_ON_WGP_MASK, 0x1f);
        }
    }
    gfx_v10_0_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff, 0);
}
```

---

## 4. Linux vs Windows — Kodėl Linux Gali O Windows Ne

### 4.1 Boot Seka Lyginimas

```
Linux:                                  Windows:
────────                                ────────
Power ON                                Power ON
  ↓                                       ↓
UEFI POST                               UEFI POST
  ↓                                       ↓
Linux kernel boot                       Windows boot
  ↓                                       ↓
amdgpu driver load                      Basic Display
  ↓                                       ↓
gfx_v10_0_get_cu_info()                 (laukia amdgpu)
  ↓                                       ↓
SPI_PG = 0x1F ← VEIKIA!                 amdgpu driver load (per vėlai!)
  ↓                                       ↓
SOS boot                                SOS boot
  ↓                                       ↓
SPI_PG jau 0x1F                         SPI_PG vis rašo
                                          ↓
                                        Grąžina 0x0 ← LOCKED!
```

### 4.2 Kritinis Skirtumas

| Aspektas | Linux | Windows |
|---|---|---|
| **Driver load** | Initramfs (labai anksti) | Po OS boot (vėliau) |
| **SPI_PG timing** | Prieš SOS lock | Po SOS lock |
| **Rezultatas** | ✅ Write prilaiko | ❌ Write neprilaiko |

### 4.3 Kodės SPI_PG Apsaugotas

SPI_PG_ENABLE_STATIC_WGP_MASK yra apsaugotas **SOS firmware**:
- SOS boot metu užrakina register
- Host BAR5 writes atmetami (readback = 0)
- Tik "legit" PSP/SOS procesas gali rašite

Linux amdgpu rašo **prieš** SOS pilną veikimą — tai vienintelis momentas kai register writable.

### 4.4 Windows WDM Limitacija

Windows WDM driver:
- Pakraunamas po OS boot (per vėlai)
- SOS jau veikia ir užrakino registrus
- Net UEFI Shell per vėlai (SOS jau bootavės)
- Net PSP driver proxy nepadėjo (tas pats SOS lock)

---

## 5. Patch'ai Palyginimas

| Patch'as | Linux | Windows |
|---|---|---|
| **40 CU unlock** | ✅ Kernel parameter (bc250_cc_write_mode=3) | ❌ SOS-locked |
| **IP offsets** | ✅ Rankiniai (cyan_skillfish_reg_init.c) | ✅ Kitas mechanizmas |
| **SMU** | ✅ cyan-skillfish-governor | ✅ Per NBIO SMN |
| **DPM** | ✅ Patch 01-03 | ✅ Per SMU Q0/Q3 |
| **Telemetry** | ✅ Patch 04-05, 11 | ✅ Per SMU mailbox |

---

## 6. SPI_PG Registro Statusas

| Reikšmė | Prasmė |
|---|---|
| 0x00000000 | Host readafter write (LOCKED) |
| 0x00000007 | Stock 24 CU (WGP 0-2) |
| 0x0000001F | Unlocked 40 CU (WGP 0-4) |
| 0xFFFFFFFF | RLC_PG (visi WGPs powered) |

---

## 7. Kernel Versijų Matrix

| Kernel | Statusas | Pastabos |
|---|---|---|
| 6.10.x | ⚠️ Works | `amdgpu.sg_display=0` reikalingas |
| 6.11.x | ✅ Good | Mesa 25.1+ reikalingas |
| 6.12.x LTS | ✅ Good | Stable fallback |
| 6.13.x | ✅ Good | Stable |
| 6.14.x LTS | ✅ Good | Well-tested |
| 6.15.0-6.15.6 | ❌ Broken | GPU init fails |
| 6.15.7-6.15.x | ✅ Recommended | Kernel support fixed |
| 6.16.x | ✅ Recommended | Full compatibility |
| 6.17.0-6.17.7 | ✅ Recommended | Good support |
| 6.17.8-6.17.10 | ❌ Broken | GPU driver broken |
| 6.17.11+ | ✅ Recommended | Kernel fix applied |
| 6.18.x LTS | ✅ Best | 6.18.18 LTS, 5-10% greitesnis |
| 6.19.x | ✅ Good | Current stable |
| 7.0-rc | 🔬 Mainline | Netestuota BC-250 |

---

## 8. Patch'ų Šaltiniai

| Šaltinis | URL |
|---|---|
| AMD Patch'ai | https://lists.freedesktop.org/archives/amd-gfx/2025-August/ |
| 40 CU Unlock | https://github.com/duggasco/bc250-40cu-unlock |
| APU Liberation | https://github.com/cachenetics/project-ariel |
| BC250 Docs | https://elektricm.github.io/amd-bc250-docs/ |
| Community Guide | https://github.com/katzzero/bc250-unofficial-community-guide |
| Kernel LTS | https://aur.archlinux.org/packages/linux-lts-amd-bc250 |

---

## 9. Išvados

1. **Linux gali unlock 40 CU** nes amdgpu driver rašo SPI_PG prieš SOS lock
2. **Windows WDM negali** nes driver pakraunamas per vėlai
3. **DXE Driver yra vienintelis Windows kelias** — veikia prieš SOS boot
4. **Reikia EDK II build environment** DXE driver kūrimui
5. **SPI_PG yra SOS-locked**, ne hardware-fused — unlock įmanomas tik tinkamu momentu

---

## 10. Nuorodos į Kitus Dokumentus

- `AGENTS.md` — bendra projekto informacija
- `docs/PSP-PROXY-BYPASS.md` — PSP proxy istorija
- `docs/BC250-3D-ENABLEMENT-ANALYSIS.md` — pilna analizė ir PSP Bridge planas
- `docs/BREAKTHROUGH.md` — atradimų istorija

---

*Document generated 2026-08-07*
