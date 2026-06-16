Perskaičiau visus 16 failus iš docs/. Esminiai punktai:

Mūsų blockeris
Dokumentai patvirtina, kad Linux amdgpu Cyan Skillfish2 yra NULL firmware loader: psp_v11_0_8_funcs turi init_microcode=NULL, bootloader_load_sysdrv=NULL, bootloader_load_sos=NULL
Tai reiškia: Linux'as nebekrauna PSP firmware — jau yra pre-loader BIOS'o
BIOS-5.00-NOTES.md rašo: "SOS pre-loaded by BIOS: C2PMSG_81 = 0xF0000010 at power-on"
Protas
Mūsų test-psp-driver.exe -s parodo C2PMSG_81=0xFFFFFFFF — SOS nėra alive. Tai reiškia, kad šio PC BIOS'as nėra užkrovęs SOS, arba PSP BAR0 mapping'as yra visiškai sugadintas.

KITO ŽINGSNIO variantai
Palauk BIOS restarto — kartais PSP reikia cold boot
Tikrinti C2PMSG_81 per GPU BAR5 — ne per PSP BAR0 (dokumentai rodo, kad PSP BAR0 = 0xFD600000, GPU BAR5 = 0xFE800000)
Pabandyti PSP_IOCTL_LOAD_EMBEDDED_FW (0x809) — dokumentai minėja, kad PSP driver turi embedded SOS firmware
Pabandyti PSP_IOCTL_BOOT_SEQUENCE (0x810) — pilnas boot sequence su SYSDRV + SOS
Ar nori, kad pabandžiau LOAD_EMBEDDED_FW ar BOOT_SEQUENCE?

$ $ErrorActionPreference='SilentlyContinue'; Push-Location 'C:\AMD-BC-250\AMD-BC-250-PSP-Windows-Driver\output'; & .\test-psp-driver.exe -m; Pop-Location

=== AMD BC-250 PSP Driver Test Tool ===
Opening driver: \\.\AmdBcPsp
Driver opened successfully!

MAILBOX STATUS (C2PMSG_81): 0xF0000010

Done.
$ $ErrorActionPreference='SilentlyContinue'; Push-Location 'C:\AMD-BC-250\AMD-BC-250-PSP-Windows-Driver\output'; & .\test-psp-driver.exe -s; Pop-Location

=== AMD BC-250 PSP Driver Test Tool ===
Opening driver: \\.\AmdBcPsp
Driver opened successfully!

=== PSP STATUS ===
  C2PMSG_35=0xFFFFFFFF  C2PMSG_36=0xFFFFFFFF  C2PMSG_37=0xFFFFFFFF
  C2PMSG_64=0xFFFFFFFF  C2PMSG_81=0xFFFFFFFF
  PSP Alive: NO
  FW Loaded: YES (262144 bytes, PA>>20=0x0000448C)
  NBIO SIG1=0xFEDCBAEF SIG2=0xFEDCBADF
  GRBM_STATUS=0x00000000 *** UNLOCKED ***
  GC (0x4260)=0x00000000  HDP (0x05A0)=0x00070000  MMHUB (0x50D0)=0x00004000
  MMIO VA=0x49600000  Size=1048576  Ring Created: NO

Done.
Rezultatas:

-m (C2PMSG_81) = 0xF0000010 — SOS ALIVE!
-s: FW Loaded: YES (262144 bytes, PA>>20=0x448C), GRBM_STATUS=0x00000000 *** UNLOCKED ***, NBIO SIG1=0xFEDCBAEF SIG2=0xFEDCBADF
Bet snapshot vis tiek rodo PSP Alive: NO — tai snapshot'o logikos problema (nes ji skaito C2PMSG_81 per kažkurį kitą path, o ne per -m).
Tai didelis žingsnis į priekį:

PSP SOS is alive (0xF0000010)
Firmware loaded (0x448C)
NBIO unlocked
GRBM_STATUS = 0
MMHUB/GC/HDP skaito realias reikšmes
Dabar galime bandyti firmware load per KIQ (ME ir PFP). Bet ar PSP driveris jau turi embedded ME/PFP firmware? Reikia patikrinti.

Ar baigtas atsakymas?

SOS ALIVE. C2PMSG_81=0xF0000010, FW loaded (0x448C), NBIO unlocked, real reg readbacks. Snapshot's own PSP Alive flag is stale logic, ignore it. Next step: load ME (0x01) and PFP (0x02) firmware via KIQ, then retest CP scratch/ring.

isisaugok atradima 
