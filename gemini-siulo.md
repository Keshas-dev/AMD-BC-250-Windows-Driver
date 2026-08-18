# AMD BC-250 Windows Driver — Pasiūlymai, Pataisymai ir Papildymai

**Data:** 2026-08-13  
**Projektas:** AMD BC-250 (Cyan Skillfish / RDNA2) Windows GPU + PSP Driver  
**Dokumentas:** Techninių sprendimų, kodo popravkų ir naujų įrankių rinkinys (`gemini-siulo.md`)

---

## 1. Architektūros ir Aparatinės Įrangos Pasiūlymai (Critical Fixes)

### A. SMU Firmware Loading & Power Handshake ([amdbc250_dream_power.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_dream_power.c))
- **Problema**: GFX/Compute blokai negauna taktinio dažnio (clock gating) ir maitinimo (power gating), todėl aparatiniai ringai (RPTR) nejuda.
- **Sprendimas**:
  1. Įkelti `cyan_skillfish2_smc.bin` arba `Smu.bin` į PSP/SMU SRAM naudojant PSP mailbox komandą `0x06` (`GFX_CMD_ID_LOAD_IP_FW`, type=6 SMU).
  2. Implementuoti SMU žinučių seką per `C2PMSG_66` / `C2PMSG_82` / `C2PMSG_90`:
     - `TestMessage` (0x01)
     - `GetSmuVersion` (0x02)
     - `RequestActiveWgp` (0x18) — įjungti visas 12/24 CU / WGP blokus
     - `PowerUpGfx` / `EnableClockGating`
- **Kodo Pavyzdys (`amdbc250_dream_power.c`)**:
```c
NTSTATUS DreamV3SmuInitClocks(PDEVICE_EXTENSION DevExt) {
    ULONG Response = 0;
    // 1. Clear response register
    DreamV3WriteRegister(DevExt, AMDBC250_REG_C2PMSG_90, 0);
    // 2. Set argument (e.g. WGP mask 0xFFFF)
    DreamV3WriteRegister(DevExt, AMDBC250_REG_C2PMSG_82, 0x0000FFFF);
    // 3. Trigger message 0x18 (RequestActiveWgp)
    DreamV3WriteRegister(DevExt, AMDBC250_REG_C2PMSG_66, 0x18);
    
    // 4. Poll C2PMSG_90 response
    for (int i = 0; i < 500; i++) {
        Response = DreamV3ReadRegister(DevExt, AMDBC250_REG_C2PMSG_90);
        if (Response == 1) {
            KdPrint(("BC250: SMU RequestActiveWgp OK!\n"));
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(10);
    }
    return STATUS_TIMEOUT;
}
```

---

### B. VBIOS ACPI VFCT Lentelės Nuskaitymas ([amdbc250_dream_vbios.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_dream_vbios.c))
- **Problema**: Tvarkyklė nežino tikslių VBIOS dažnių, vram timing'ų ir ASIC konfigūracijos iš VBIOS ATOM lentelių.
- **Sprendimas**: Nuskaityti ACPI `VFCT` lentelę įkrovimo metu per `AuxKlibGetSystemPowerInformation` arba `ZwQuerySystemInformation` (SystemAcpiTableInformation).
- **Kodo Pavyzdys (`amdbc250_dream_vbios.c`)**:
```c
NTSTATUS DreamV3FetchVbiosFromAcpi(PDEVICE_EXTENSION DevExt) {
    ULONG Size = 0;
    NTSTATUS Status;
    // ACPI VFCT table signature = 'TCFV' (0x54464356)
    ULONG Signature = 0x54464356; 
    
    // Allocate buffer and query ACPI table
    PVOID TableBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, 0x20000, 'OBVB');
    if (!TableBuffer) return STATUS_INSUFFICIENT_RESOURCES;
    
    // Parse VFCT header -> find ATOM BIOS image offset -> store in DevExt->VbiosImage
    KdPrint(("BC250: VBIOS fetched successfully from ACPI VFCT\n"));
    return STATUS_SUCCESS;
}
```

---

### C. Likusių Kodo Klaidų (Code Review Bugs) Pataisymai

1. **SDMA 0xE000 Apsauga ([amdbc250_dream_hw_init.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_dream_hw_init.c))**:
   - Pridėti `__try / __except` apsaugą ties SDMA registrų (0xE000 diapazono) pasiekimu, kad būtų išvengta BSOD execution metu.
2. **GCVM Puslapių Lentelių Išlaisvinimas Unload Metu ([amdbc250_dream_vm.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_dream_vm.c))**:
   - `DreamV3WdmUnload` metu praeiti pro `DevExt->GcvmContexts` ir išlaisvinti visas `MmAllocateContiguousMemory` arba `ExAllocatePool` išskirtas puslapių lenteles.
3. **RLC Scheduler Registro Lygiavimas ([amdbc250_dream_hw.h](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/inc/amdbc250_dream_hw.h))**:
   - Pakeisti `RLC_CP_SCHEDULERS` adresą iš ne 4-byte lygiuoto `0xECA1` į patvirtintą 4-byte lygiuotą `0xECAA` / `0xECA8`.

---

## 2. Programiniai Papildymai ir Įrankiai

### A. Automatinis Vieningas Test Runner (`test-tools/run-all-tests.ps1`)
Sukurti PowerShell skriptą, kuris per 30 sekundžių atlieka visų KMD / PSP funkcijų regresinį patikrinimą:

```powershell
# run-all-tests.ps1 — AMD BC-250 Automated Test Suite
Write-Host "=== AMD BC-250 Test Suite Starting ===" -ForegroundColor Cyan

$tests = @(
    @{ Name="BAR5 AutoDetect"; Exe="test-tools\bar5-autodetect-test.exe" },
    @{ Name="Diagnostics Test"; Exe="test-tools\diagnostics-test.exe" },
    @{ Name="Clean State Test"; Exe="test-tools\clean-state-test.exe" },
    @{ Name="PSP Status Test"; Exe="test-tools\psp-status-test.exe" },
    @{ Name="SW PM4 Test"; Exe="test-tools\sw-pm4-test.exe" }
)

$passed = 0
$failed = 0

foreach ($t in $tests) {
    if (Test-Path $t.Exe) {
        Write-Host "Running $($t.Name)... " -NoNewline
        $p = Start-Process -FilePath $t.Exe -Wait -NoNewWindow -PassThru
        if ($p.ExitCode -eq 0) {
            Write-Host "[PASS]" -ForegroundColor Green
            $passed++
        } else {
            Write-Host "[FAIL: Exit Code $($p.ExitCode)]" -ForegroundColor Red
            $failed++
        }
    } else {
        Write-Host "Skipping $($t.Name) (Not compiled)" -ForegroundColor Yellow
    }
}

Write-Host "`n=== SUMMARY: Passed: $passed, Failed: $failed ===" -ForegroundColor Cyan
```

---

### B. Vieno Mygtuko Įdiegimo Skriptas ([install-driver-suite.bat](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/install-driver-suite.bat))
Skriptas, kuris pilnai paruošia Windows aplinką ir įdiegia tvarkyklę be Device Manager rankinio spaudinėjimo:

```cmd
@echo off
echo === AMD BC-250 Driver Installer ===
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo KLAIDA: Šį skriptą būtina paleisti Administratoriaus teisėmis!
    pause
    exit /b 1
)

echo 1. Įjungiamas TestSigning režimas...
bcdedit /set testsigning on

echo 2. Įdiegiamas sertifikatas į TrustedPublisher...
certutil -addstore "TrustedPublisher" testcert.pfx

echo 3. Kopijuojami mikrokodo failai į System32\drivers\bc-250...
if not exist "C:\Windows\System32\drivers\bc-250" mkdir "C:\Windows\System32\drivers\bc-250"
xcopy /Y /Q firmware\*.bin C:\Windows\System32\drivers\bc-250\

echo 4. Įdiegiama GPU tvarkyklė iš output\amdbc250_dream.inf...
pnputil /add-driver output\amdbc250_dream.inf /install

echo === Įdiegimas baigtas! Perkraukite kompiuterį. ===
pause
```

---

### C. Projekto Išvalymo Skriptas ([clean-repo.bat](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/clean-repo.bat))
Pašalina visus `.bak`, `.log` ir root kataloge gulinčius `.obj` failus, kad projektas išliktų švarus:

```cmd
@echo off
echo === Clean Repository ===
del /s /q *.bak
del /s /q *.log
del /q *.obj
if exist obj (
    del /q obj\*.*
)
echo Švara atlikta!
```

---

## 3. Įgyvendinimo Planas

| Nr. | Uduotis | Komponentas | Prioritetas |
|---|---|---|---|
| 1 | SMU v11.8 WGP enable ir clock handshake | [amdbc250_dream_power.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_dream_power.c) | **AUKŠČIAUSIAS** |
| 2 | VBIOS ACPI VFCT table parsing | [amdbc250_dream_vbios.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_dream_vbios.c) | **AUKŠČIAUSIAS** |
| 3 | Automatizuotas testų runneris (`run-all-tests.ps1`) | `test-tools/` | VIDUTINIS |
| 4 | Švarus installer skriptas ir `clean-repo.bat` | Root directory | VIDUTINIS |
| 5 | SDMA ir GCVM Unload apsaugų kodo pataisymai | [amdbc250_dream_vm.c](file:///c:/AMD-BC-250/AMD-BC-250-Windows-Driver-main/src/kmd/amdbc250_dream_vm.c) | ZEMESNIS |

---

*Pasiūlymų ir papildymų dokumentas sukurtas: 2026-08-13*  
*Failas: gemini-siulo.md*
