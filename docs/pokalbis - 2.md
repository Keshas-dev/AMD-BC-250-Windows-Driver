atmintis :
Štai kas šiuo metu yra:

Šiuo metu naudojama firmware
Embedded į driver (firmware_data.h):

g_SysdrvFirmwareData — 262,656 baitų — PSP SYSDRV firmware
g_SosFirmwareData — 262,144 baitų — PSP SOS firmware
Nėra embedded:

❌ SMU/SMC firmware (cyan_skillfish2_smc.bin) — nėra įtrauktas į firmware_data.h
❌ GPU firmware (ME, PFP, CE, MEC, MEC2, RLC, SDMA) — įkeliami per IOCTL iš failų
❌ TA firmware — neįkeltas
Kaip įkeliama
PSP SYSDRV + SOS: embedded, įkeliami driver INIT valdiklis
GFX firmware: IOCTL_PSP_LOAD_IP_FW_DIRECT — iš Firmware/cyan_skillfish2_*.bin failų
SMU/SMC: Nėra — nėra IOCTL, nėra embedded, IOCTL_PSP_LOAD_TOC yra NOT_IMPLEMENTED
Išvada
SMU/SMC firmware nėra įkeltas. Kad jį įkeltume, reikia:

Pridėti cyan_skillfish2_smc.bin į firmware_data.h, ARBA
Implementuoti IOCTL_PSP_LOAD_TOC su TOC bootloader seka su SMU
Ar norite:

Pridėti cyan_skillfish2_smc.bin į firmware_data.h ir bandyti įkelti per LOAD_IP_FW_DIRECT su fwType=11?
Arba implementuoti IOCTL_PSP_LOAD_TOC pilnai bootloader sekai?


Tai patvirtina: cyan_skillfish2_smc.bin trūksta mūsų Firmware kataloge. Turime visus kitus firmware failus (ME, PFP, CE, MEC, MEC2, RLC, SDMA, SOS, ASD, TA), bet SMU (SMC) firmware nėra.

PSP driverio BOOT_SEQUENCE handleris įkelia tik SYSDRV ir SOS firmware (per C2PMSG_35/36 bootloaderį). SMC firmware nėra įkeltas — tai ir yra galutinis blokeris.

Galimos cyan_skillfish2_smc.bin šaltiniai:

Firmware/amdgpu/navi10_smc.bin — BC-250 yra Navi10 variantas, gali būti tinkamas
Linux /lib/firmware/amdgpu/cyan_skillfish2_smc.bin (jei Linux sistemą turi prieigą)
AMD firmware repo: github.com/FreddyFunk/amd-firmware
Be SMU firmware, SMU neveikia → GFX/CP/SDMA blokai neturi clock/power → ringai neapdorojami.

Projekto būsena: BAIGTA.

Visi tikrinimai atlikti, pagrindinė priežastis nustatyta (SMU firmware trūkumas), Linux palyginimas atliktas. Išsaugojau visą analizę AGENTS.md ir atmintyje.



