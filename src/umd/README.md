# User-Mode Driver (UMD) Source

## ✅ ACTIVE BUILD (kompiliuojama į amdbc250umd64.dll)

1. **amdbc250_umd_v46.c** (1,041 lines)
   - Full resource management
   - Heap allocation, resource mapping, descriptor heaps
   - **Status:** ✅ PRIMARY BUILD SOURCE

## ⚠️ LEGACY / ARCHIVE SOURCES

Older UMD versions and historical sources have been moved to `archive\legacy-umd\`.
Only `amdbc250_umd_v46.c` is used by the active build script.

## ⚠️ NOT COMPILED (parašyta, bet nekompiliuota)

4. **amdbc250_umd_full.c** (336 lines)
   - D3D10/11/12 stubs
   - More complete than v41
   - **Status:** ⚠️ READY TO COMPILE

5. **amdbc250_umd_d3d12.c** (185 lines)
   - D3D12 specific implementation
   - CreateDevice, CreateCommandList, etc.
   - **Status:** ⚠️ READY TO COMPILE

## ❌ OLD/DEPRECATED (nenaudojamas)

6. **amdbc250_umd_minimal.c** (55 lines)
   - Originalus minimalus stub
   - **Status:** ❌ REPLACED BY v41/v46

## 📦 EXTRA DIRECTORY

Pilnas UMD source yra `extra/amdbc250_umd.c` (1,670 lines):
- D3D9/10/11/12 pilnas implementation
- 99 KB source code
- **Status:** ⚠️ NOT COMPILED (needs integration)

## Kaip kompiliuoti:

### Dabartinis (v46 / latest):
```powershell
.\build.bat
```

### Pilnas UMD (future):
```powershell
# Use build.bat or update to a future UMD-only script if added
.\build.bat
```

Arba rankiniu būdu (v46):
```powershell
cl.exe /c /TC /D_AMD64_ /DWIN64 /DAMDBC250_UMD /W3 /Zi /O2 ^
  /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um" ^
  /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared" ^
  /I"inc" src\umd\amdbc250_umd_v46.c

link.exe /DLL /OUT:amdbc250umd64.dll build-output\amdbc250_umd.obj ^
  d3d12.lib dxgi.lib dxguid.lib user32.lib
```

---

**Build Status:** ✅ Latest available UMD source selected automatically by build script
