# BC-250 P5.00 BIOS Settings Reference

## Critical for PSP/NBIO Access

| Setting | Location | Recommended | What it does |
|---------|----------|-------------|--------------|
| IOMMU | Advanced > CPU Config | **Disabled** | Išjungia IOMMU apsaugą |
| PSP Support | Advanced > AMD CBS > NBIO | **Enabled** (Platform First) | PSP inicializavimo tvarka |
| fTPM | Advanced > AMD CBS > TPM | **Disabled** | Trusted Platform Module |
| Secure Boot | Boot > Secure Boot | **Disabled** | UEFI Secure Boot |
| Above 4G Decoding | Advanced > PCI Subsystem | **Enabled** | Leidžia 64-bit DMA |
| ReBAR | Advanced > PCI Subsystem | **Enabled** | Resizable BAR |

## GFX Configuration (Chipset)

| Setting | Location | Recommended | What it does |
|---------|----------|-------------|--------------|
| Integrated Graphics | Chipset > GFX Config | **Forces** | Įjungia iGPU |
| UMA Mode | Chipset > GFX Config | **UMA_SPECIFIED** | Rankinis VRAM nustatymas |
| UMA Frame Buffer Size | Chipset > GFX Config | **512MB** | Dinaminis VRAM |

## Advanced AMD CBS Options

| Setting | Location | What it does |
|---------|----------|-------------|
| NBIO Common Options | CBS > NBIO | NBIO konfigūracija |
| Device Exclusion Vector | CBS > NBIO > DEV | Aparatūros užkarda |
| SMU Configuration | CBS > SMU | SMU valdymas |
| PSP Configuration | CBS > PSP | PSP nustatymai |
| Platform First | CBS > Platform | Inicializavimo tvarka |

## Safe Settings to Try

1. **PSP Support = Disabled** - Windows nenaudos PSP
2. **Platform First = Enabled** - PSP inicializuosis prieš OS
3. **Device Exclusion Vector = Disabled** - Išjungs NBIO užkardą
4. **fTPM = Disabled** - Atsakingas už saugumo funkcijas

## Danger Zones (DO NOT TOUCH)

- Voltage settings
- Memory timings
- Clock multipliers
- Debug settings
- Any "Auto" overclocking

## Test Sequence After Each Change

1. Išsaugokite BIOS nustatymus (F10)
2. Paleiskite Windows
3. Paleiskite PSP testą: `output\test-psp-init.exe`
4. Jei PSP veikia - sėkmė!
5. Jei ne - grįžkite į BIOS ir bandykite kitą nustatymą