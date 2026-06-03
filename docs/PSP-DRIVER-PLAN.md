AMD BC-250 PSP Driver Architecture
==================================

Current problem:
- GPU driver (atikmdag.sys) bando inicializuoti PSP
- NBIO firewall blokuoja prieigą prie PSP MMIO
- Firmware įkomponuota, bet neveikia

Key insight:
- PSP yra ATSKIRAS PCI device (VEN_1022&DEV_143E)
- Jam REIKIA savo driverio
- GPU driveris (atikmdag.sys) skirtas TIK GPU (VEN_1002&DEV_13FE)

Plan:
1. Sukurti atskirą PSP driverį (pci_psp.sys)
2. Jis būtų skirtas PCI\VEN_1022&DEV_143E
3. Su embedded firmware
4. Su prieiga per PSP specifinius registrus
5. StartType = 0 (BOOT_START)

Galbūt PSP device turi KITOKIĄ prieigą nei GPU,
ir NBIO užkarda jo neblokuoja?