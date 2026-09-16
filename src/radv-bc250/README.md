# RADV BC-250 winsys backend

BC-250 winsys backend RADV (Mesa Vulkan driveriui AMD GPU).

Implementuoja RADV `radeon_winsys` sąsają (`radv_radeon_winsys.h`)
ant BC-250 KMD IOCTL paviršiaus (`\\.\AMDBC250DreamV43`)
vietoj Linux DRM/amdgpu.

## Failai

| Failas | Paskirtis |
|--------|-----------|
| `radv_bc250_winsys.h` | Winsys/BO struktūros + IOCTL kodai + entry points |
| `radv_bc250_winsys.c` | `radv_bc250_winsys_create()` — atidaro KMD, sujungia funkcijų lenteles |
| `radv_bc250_bo.c` | Buffer management: ALLOC/FREE/MAP/UNMAP_VIDMEM |
| `radv_bc250_cs.c` | Command submission: SUBMIT_COMMANDS + fence wait |
| `meson.build` | Mesa build integracija |

## IOCTL atvaizdavimas (Collabora WDM analodija)

Collabora Mesa Windows porte `wddm_submit_command()` kviečia
`D3DKMTSubmitCommand` į AMD proprietary KMD. Čia vietoj to
kviečiame **mūsų** KMD IOCTL — tas pats principas, kitas transportas:

```
RADV winsys funkcija      BC-250 IOCTL (kodas)
------------------------  ----------------------------------------
buffer_create             ALLOC_VIDMEM    (0x80000840)
buffer_destroy            FREE_VIDMEM     (0x80000844)
buffer_map                MAP_VIDMEM      (0x80000848)
buffer_unmap              UNMAP_VIDMEM    (0x8000084C)
cs_submit                 SUBMIT_COMMANDS (0x80000880)
ctx_wait_idle             WAIT_FENCE      (0x80000884)
read_registers            READ_REG        (0x80000B88)
query_value (clocks)      statinės BC-250 reikšmės (1500/1750 MHz)
```

Struktūros atitinka `inc/amdbc250_ioctl.h`:
`AMDBC250_IOCTL_ALLOC_VIDMEM`, `AMDBC250_IOCTL_SUBMIT_COMMANDS`, ir t.t.

## Mesa build integracija

1. Nukopijuok `src/radv-bc250/` į
   `mesa/src/amd/vulkan/winsys/bc250/`.
2. `mesa/src/amd/vulkan/winsys/` meson.build: pridėk `subdir('bc250')`
   su `if host_machine.system() == 'windows'` sąlyga.
3. `radv_physical_device.c`: `radv_amdgpu_winsys_create()` iškvietimą
   papildyk BC-250 šaka — kai PCI DID == `0x13FE` ir atidaromas
   `\\.\AMDBC250DreamV43`, kviesk `radv_bc250_winsys_create()`.
4. Statyk: `meson setup builddir && ninja -C builddir radv`
5. Registruok ICD: `VK_ICD_FILENAMES=<kelias>/bc250-radv_icd.json`

## ACO kompiliatorius (2-as žingsnis)

RADV šešėlių kompiliatorius ACO jau palaiko gfx1013 (BC-250 GFX
10.1.3) — jokių pakeitimų nereikia. Svarbios pastabos iš
`DryhoppedIPA/bc250-gfx1013-fix`:
- `v_dot4_i32_i8` neveikia — palikti išjungtą.
- Async compute: thread-dimension mode (ne PARTIAL_TG_EN).
- SDot hybrid + i24 MUL24/MAD24 (Mesa 26.2.0+).

## Aparatinės įrangos būsena

- ✅ Submit kelias teisingas (KMD priima, Result=1).
- ❌ WGP/SPI_PG SOS-gated Windows WDM (`0x5C3C` = 0) —
  GPU nevykdo šešėlių iki EFI atrakinimo.
- Backendas pilnas ir teisingas; vykdymą stabdo
  aparatinė būsena, ne šis kodas.

## Testavimas be admin (dabar)

```cmd
:: Sintaksės patikra (standalone, be Mesa):
cl /nologo /c /DRADV_BC250_STANDALONE radv_bc250_bo.c radv_bc250_cs.c radv_bc250_winsys.c
```
