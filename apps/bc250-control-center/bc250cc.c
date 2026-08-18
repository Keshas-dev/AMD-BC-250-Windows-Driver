/* bc250cc.c — BC-250 Control Center for Windows (Win32 GUI).
 *
 * Port of the Linux bc250-control-center (PyQt6) functionality to Windows.
 * Backend: AMDBC250DreamV43 (preferred, INIT_HARDWARE + SMN) with automatic
 * fallback to the minimal AMDBC250Reg driver (\\.\AMDBC250Reg, BAR5 mapped
 * at load). All SMU mailbox traffic goes through the BAR5+0x38/0x3C SMN
 * window which both drivers expose via the same READ_REG/WRITE_REG IOCTLs.
 *
 * Tabs:
 *   Monitor    — live SMU telemetry (freq, VID, WGP, features, temps, fans),
 *                core mask, GRBM status. Auto-refresh every 1s.
 *   Governor   — GPU safe-point curve, force freq/voltage, restore
 *                (exact governor change_freq sequence, proven safe).
 *   CPU OC     — temporary CPU OC via SMU Q3 (temp + vid + per-core clock).
 *   Core unlock— SMU Q3 msg 0x98 -> SMN[0x0115A870] (8 cores / 16 threads).
 *   CU status  — read SPI_PG / CC_ARRAY / RLC_PG + attempt 40CU unlock
 *                (reports SOS locking as expected on Windows).
 *   Registers  — direct BAR5 read/write + SMN read/write.
 *
 * Build: compile-bc250cc.bat
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "..\..\inc\amdbc250_ioctl.h"

/* ------------------------------------------------------------------ */
/* Driver backend: try DreamV43, fall back to AMDBC250Reg              */
/* ------------------------------------------------------------------ */
#define DREAM_DEV   "\\\\.\\AMDBC250DreamV43"
#define REG_DEV     "\\\\.\\AMDBC250Reg"

static HANDLE   g_h = INVALID_HANDLE_VALUE;
static char     g_driverName[128] = "not connected";

/* Read/write a 32-bit BAR5 register through whichever driver is open.
 * Both drivers use the identical IOCTL layout. */
static BOOL DrvW32(uint32_t off, uint32_t val)
{
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b;
    r.RegisterOffset = off; r.Value = val;
    return DeviceIoControl(g_h, IOCTL_AMDBC250_WRITE_REG,
                           &r,sizeof(r),&r,sizeof(r),&b,NULL);
}
static uint32_t DrvR32(uint32_t off)
{
    AMDBC250_IOCTL_REG_ACCESS r; DWORD b;
    r.RegisterOffset = off; r.Value = 0;
    if (DeviceIoControl(g_h, IOCTL_AMDBC250_READ_REG,
                        &r,sizeof(r),&r,sizeof(r),&b,NULL)) return r.Value;
    return 0xFFFFFFFFu;
}

/* SMN transport via NBIO BAR5 0x38/0x3C (same on both drivers). */
static void smnW(uint32_t a, uint32_t v){ DrvW32(0x38,a); DrvW32(0x3C,v); }
static uint32_t smnR(uint32_t a){ DrvW32(0x38,a); DrvR32(0x38); return DrvR32(0x3C); }

static int BackendOpen(void)
{
    HANDLE h;

    /* Preferred: DreamV43 (needs INIT_HARDWARE to map BAR5 on Win11). */
    h = CreateFileA(DREAM_DEV, GENERIC_READ|GENERIC_WRITE,
                    FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        AMDBC250_IOCTL_INIT_HARDWARE ih; DWORD br = 0;
        ZeroMemory(&ih, sizeof(ih));
        ih.MmioPhysicalBase = 0xFE800000ULL;
        ih.MmioSize         = 0x80000;
        ih.Flags            = AMDBC250_INIT_FLAG_NBIO_MAP;
        if (DeviceIoControl(h, IOCTL_AMDBC250_INIT_HARDWARE,
                            &ih,sizeof(ih),&ih,sizeof(ih),&br,NULL)) {
            g_h = h;
            strcpy_s(g_driverName, sizeof(g_driverName), "AMDBC250DreamV43 (INIT_HARDWARE OK)");
            return 1;
        }
        CloseHandle(h);
    }

    /* Fallback: minimal AMDBC250Reg (BAR5 mapped at load). */
    h = CreateFileA(REG_DEV, GENERIC_READ|GENERIC_WRITE,
                    FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        g_h = h;
        strcpy_s(g_driverName, sizeof(g_driverName), "AMDBC250Reg (BAR5 mapped at load)");
        return 1;
    }

    return 0;
}

static void BackendClose(void)
{
    if (g_h != INVALID_HANDLE_VALUE) { CloseHandle(g_h); g_h = INVALID_HANDLE_VALUE; }
}

/* ------------------------------------------------------------------ */
/* SMU mailbox: Queue 0 / Queue 2 / Queue 3                           */
/* Return: 1=OK, -1=FAIL(0xFF), -2=UNKNOWN(0xFE), -3=REJECTED(0xFD),  */
/*         -4=BUSY(0xFC), -100=TIMEOUT                                */
/* ------------------------------------------------------------------ */
#define Q0_CMD 0x03B10A08
#define Q0_RSP 0x03B10A68
#define Q0_ARG 0x03B10A48

#define Q2_CMD 0x03B10528
#define Q2_RSP 0x03B10564
#define Q2_ARG 0x03B10998
#define Q2_ARH 0x03B1099C

#define Q3_CMD 0x03B10A20
#define Q3_RSP 0x03B10A80
#define Q3_ARG 0x03B10A88

static int q0(uint32_t msg, uint32_t arg)
{
    smnW(Q0_RSP,0); smnW(Q0_ARG,arg); smnW(Q0_CMD,msg);
    for (int i=0;i<500;i++){
        uint32_t st=smnR(Q0_RSP);
        if (st==1) return 1; if (st==0xFF) return -1; if (st==0xFE) return -2;
        if (st==0xFD) return -3; if (st==0xFC) return -4; Sleep(1);
    }
    return -100;
}
static uint32_t q0_arg(void){ return smnR(Q0_ARG); }

static int q2(uint32_t msg, uint32_t arg, uint32_t argHi)
{
    smnW(Q2_RSP,0); smnW(Q2_ARG,arg); smnW(Q2_ARH,argHi); smnW(Q2_CMD,msg);
    for (int i=0;i<500;i++){
        uint32_t st=smnR(Q2_RSP);
        if (st==1) return 1; if (st==0xFF) return -1; if (st==0xFE) return -2;
        if (st==0xFD) return -3; if (st==0xFC) return -4; Sleep(1);
    }
    return -100;
}
static uint32_t q2_arg(void){ return smnR(Q2_ARG); }

static int q3(uint32_t msg, uint32_t arg)
{
    smnW(Q3_RSP,0); smnW(Q3_ARG,arg); smnW(Q3_CMD,msg);
    for (int i=0;i<500;i++){
        uint32_t st=smnR(Q3_RSP);
        if (st==1) return 1; if (st==0xFF) return -1; if (st==0xFE) return -2;
        if (st==0xFD) return -3; if (st==0xFC) return -4; Sleep(1);
    }
    return -100;
}
static uint32_t q3_arg(void){ return smnR(Q3_ARG); }

static int q3_wait_result(uint32_t msg, uint32_t arg, int timeoutMs)
{
    smnW(Q3_RSP,0); smnW(Q3_ARG,arg); smnW(Q3_CMD,msg);
    DWORD end=GetTickCount()+ (DWORD)timeoutMs;
    for (;;){
        uint32_t st=smnR(Q3_RSP);
        if (st==1) return 1; if (st==0xFF) return -1; if (st==0xFE) return -2;
        if (st==0xFD) return -3; if (st==0xFC) return -4;
        if (GetTickCount()>end) return -100;
        Sleep(2);
    }
}

/* VID <-> mV (SMU v11.8 formula). */
static int vid_to_mv(int vid){ return (int)((-vid*0.00625 + 1.55) * 1000 + 0.5); }
static int mv_to_vid(int mv){ return (int)((1.55 - (double)mv/1000.0) / 0.00625 + 0.5); }

static int popcount32(uint32_t v){ int c=0; while(v){ c += v&1; v>>=1; } return c; }

/* Governor default safe points (v0.4.11 default-config.toml). */
static const struct { int freq; int mv; } SAFE_POINTS[] = {
    { 500, 700},{1000, 800},{1175, 850},{1500, 900},{1600, 910},{1700, 920},
    {1850, 930},{2000, 960},{2050, 980},{2100,1000},{2125,1020},{2150,1035},
    {2200,1050},{2230,1085},{2300,1110},{2350,1130},{2400,1150},
};
#define N_SAFE (int)(sizeof(SAFE_POINTS)/sizeof(SAFE_POINTS[0]))

/* ------------------------------------------------------------------ */
/* GUI                                                                */
/* ------------------------------------------------------------------ */
#define IDC_TAB           1001

/* Monitor */
#define IDC_M_DRIVER      1100
#define IDC_M_GPU_ID      1101
#define IDC_M_GRBM        1102
#define IDC_M_SMUVER      1103
#define IDC_M_FREQ        1104
#define IDC_M_VID         1105
#define IDC_M_MV          1106
#define IDC_M_WGP         1107
#define IDC_M_FEAT        1108
#define IDC_M_CORES       1109
#define IDC_M_TCDGE       1110
#define IDC_M_TJUNC       1111
#define IDC_M_TMEM        1112
#define IDC_M_FAN         1113
#define IDC_M_CPUMV       1114
#define IDC_M_GPUMV       1115
#define IDC_M_REFRESH     1116
#define IDC_M_AUTO        1117

/* Governor */
#define IDC_G_FREQ        1200
#define IDC_G_MV          1201
#define IDC_G_PROFILE     1202
#define IDC_G_APPLY       1203
#define IDC_G_UNFORCE     1204
#define IDC_G_TEMP        1205
#define IDC_G_LIST        1206

/* CPU OC */
#define IDC_C_FREQ        1300
#define IDC_C_VID         1301
#define IDC_C_TEMP        1302
#define IDC_C_KIND        1303
#define IDC_C_APPLY       1304
#define IDC_C_INFO        1305

/* Core unlock */
#define IDC_K_MASK        1400
#define IDC_K_UNLOCK      1401
#define IDC_K_RESULT      1402

/* CU status */
#define IDC_U_SPI         1500
#define IDC_U_CC          1501
#define IDC_U_RLC         1502
#define IDC_U_WGP         1503
#define IDC_U_READ        1504
#define IDC_U_ATTEMPT     1505
#define IDC_U_RESULT      1506

/* Registers */
#define IDC_R_OFF         1600
#define IDC_R_VAL         1601
#define IDC_R_READ        1602
#define IDC_R_WRITE       1603
#define IDC_R_RES         1604
#define IDC_S_OFF         1605
#define IDC_S_VAL         1606
#define IDC_S_READ        1607
#define IDC_S_WRITE       1608
#define IDC_S_RES         1609

#define IDC_LOG           2001

static HWND g_hTab, g_page[6];
static HWND g_hLog;
static const char *g_tabNames[6] = {
    "Monitor", "Governor", "CPU OC", "Core unlock", "CU status", "Registers"
};
static int g_autoRefresh = 1;

static HWND AddCtrl(HWND parent, const char *cls, const char *text,
                    int x,int y,int w,int h, DWORD id, DWORD style)
{
    HWND c = CreateWindowExA(0, cls, text,
        WS_CHILD|WS_VISIBLE|style, x,y,w,h, parent,
        (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    return c;
}

static void SetLbl(HWND page, DWORD id, const char *fmt, ...)
{
    char buf[512]; va_list ap;
    va_start(ap, fmt); vsprintf_s(buf, sizeof(buf), fmt, ap); va_end(ap);
    SetDlgItemTextA(page, id, buf);
}
static void SetEdit(HWND page, DWORD id, const char *fmt, ...)
{
    char buf[128]; va_list ap;
    va_start(ap, fmt); vsprintf_s(buf, sizeof(buf), fmt, ap); va_end(ap);
    SetDlgItemTextA(page, id, buf);
}
static uint32_t GetEditU32(HWND page, DWORD id, uint32_t def)
{
    char buf[64];
    if (!GetDlgItemTextA(page, id, buf, sizeof(buf))) return def;
    return (uint32_t)strtoul(buf, NULL, 0);
}

/* ---- pages ------------------------------------------------------- */
static HWND BuildMonitorPage(HWND parent)
{
    HWND p = CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_LEFT,
                             0,0,1,1,parent,NULL,GetModuleHandle(NULL),NULL);
    int x=12, y=10, lw=170, rw=210, rh=20;
    AddCtrl(p,"STATIC","Driver:", x,y,100,rh, 0, SS_LEFT);
    AddCtrl(p,"STATIC","", x+lw+50,y,rw,rh, IDC_M_DRIVER, SS_LEFT); y+=24;
    const struct { int id; const char* name; } rows[] = {
        { IDC_M_GPU_ID,"GPU_ID"}, { IDC_M_GRBM,"GRBM_STATUS"},
        { IDC_M_SMUVER,"SMU"},    { IDC_M_FREQ,"GFX freq"},
        { IDC_M_VID,"GFX VID"},   { IDC_M_MV,"GFX voltage"},
        { IDC_M_WGP,"Active WGPs"},{ IDC_M_FEAT,"SMU features"},
        { IDC_M_CORES,"CPU cores"},{ IDC_M_TCDGE,"Edge temp"},
        { IDC_M_TJUNC,"Junction"},{ IDC_M_TMEM,"Mem temp"},
        { IDC_M_FAN,"Fan RPM/PWM"},{ IDC_M_CPUMV,"CPU mV"},
        { IDC_M_GPUMV,"GPU mV"},
    };
    for (int i=0;i<(int)(sizeof(rows)/sizeof(rows[0]));i++){
        AddCtrl(p,"STATIC",rows[i].name, x,y,lw,rh, 0, SS_LEFT);
        AddCtrl(p,"STATIC","--", x+lw+50,y,rw,rh, rows[i].id, SS_LEFT);
        y+=22;
    }
    AddCtrl(p,"BUTTON","Refresh now", x,y+lw,110,26, IDC_M_REFRESH, BS_PUSHBUTTON);
    AddCtrl(p,"BUTTON","1s auto-refresh", x+130,y+lw,120,26, IDC_M_AUTO, BS_AUTOCHECKBOX);
    SetDlgItemTextA(p,IDC_M_AUTO,"1s auto-refresh");
    SendMessageA(GetDlgItem(p,IDC_M_AUTO), BM_SETCHECK, BST_CHECKED, 0);
    /* give the page room */
    SetWindowPos(p,NULL,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE);
    return p;
}

static HWND BuildGovernorPage(HWND parent)
{
    HWND p = CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_LEFT,
                             0,0,1,1,parent,NULL,GetModuleHandle(NULL),NULL);
    int x=12,y=10;
    AddCtrl(p,"STATIC","GPU freq (MHz):", x,y,120,20, 0, SS_LEFT);
    AddCtrl(p,"EDIT","1500", x+130,y,80,22, IDC_G_FREQ, ES_AUTOHSCROLL|WS_BORDER);
    AddCtrl(p,"STATIC","Voltage (mV):", x+230,y,110,20, 0, SS_LEFT);
    AddCtrl(p,"EDIT","900", x+350,y,80,22, IDC_G_MV, ES_AUTOHSCROLL|WS_BORDER);
    y+=30;
    AddCtrl(p,"STATIC","Perf profile (1/3):", x,y,130,20, 0, SS_LEFT);
    AddCtrl(p,"EDIT","3", x+140,y,40,22, IDC_G_PROFILE, ES_AUTOHSCROLL|WS_BORDER);
    AddCtrl(p,"BUTTON","Apply (force freq+vid)", x+200,y,170,26, IDC_G_APPLY, BS_PUSHBUTTON);
    AddCtrl(p,"BUTTON","Restore (unforce)", x+380,y,140,26, IDC_G_UNFORCE, BS_PUSHBUTTON);
    AddCtrl(p,"BUTTON","Set max temp 80C", x+530,y,140,26, IDC_G_TEMP, BS_PUSHBUTTON);
    y+=40;
    AddCtrl(p,"STATIC","Safe points (freq = voltage, built from cyan-skillfish-governor TOML):",
            x,y,700,20, 0, SS_LEFT); y+=22;
    HWND lb = CreateWindowExA(WS_EX_CLIENTEDGE,"LISTBOX","",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOINTEGRALHEIGHT|LBS_USETABSTOPS,
        x,y,700,320, p,(HMENU)(INT_PTR)IDC_G_LIST,GetModuleHandle(NULL),NULL);
    SendMessageA(lb,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
    char line[64];
    for (int i=0;i<N_SAFE;i++){
        sprintf_s(line,sizeof(line),"  %-5d MHz = %-4d mV", SAFE_POINTS[i].freq, SAFE_POINTS[i].mv);
        SendMessageA(lb,LB_ADDSTRING,0,(LPARAM)line);
    }
    return p;
}

static HWND BuildCpuOcPage(HWND parent)
{
    HWND p = CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_LEFT,
                             0,0,1,1,parent,NULL,GetModuleHandle(NULL),NULL);
    int x=12,y=10;
    AddCtrl(p,"STATIC","CPU freq (3000-4200 MHz):", x,y,180,20, IDC_C_FREQ, SS_LEFT);
    AddCtrl(p,"EDIT","3600", x+190,y,80,22, IDC_C_FREQ, ES_AUTOHSCROLL|WS_BORDER);
    y+=30;
    AddCtrl(p,"STATIC","VID (900-1375 mV):", x,y,150,20, IDC_C_VID, SS_LEFT);
    AddCtrl(p,"EDIT","1000", x+160,y,80,22, IDC_C_VID, ES_AUTOHSCROLL|WS_BORDER);
    y+=30;
    AddCtrl(p,"STATIC","CPU/GPU temp limit:", x,y,150,20, IDC_C_TEMP, SS_LEFT);
    AddCtrl(p,"EDIT","90", x+160,y,80,22, IDC_C_TEMP, ES_AUTOHSCROLL|WS_BORDER);
    y+=30;
    AddCtrl(p,"STATIC","Kind (0=CPU,1=GPU):", x,y,140,20, IDC_C_KIND, SS_LEFT);
    AddCtrl(p,"EDIT","0", x+150,y,50,22, IDC_C_KIND, ES_AUTOHSCROLL|WS_BORDER);
    y+=40;
    AddCtrl(p,"BUTTON","Apply temporary CPU OC", x,y,200,28, IDC_C_APPLY, BS_PUSHBUTTON);
    AddCtrl(p,"STATIC","Result:", x,y+38,60,20, IDC_C_INFO, SS_LEFT);
    AddCtrl(p,"STATIC","--", x+70,y+38,450,20, IDC_C_INFO, SS_LEFT);
    return p;
}

static HWND BuildCoreUnlockPage(HWND parent)
{
    HWND p = CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_LEFT,
                             0,0,1,1,parent,NULL,GetModuleHandle(NULL),NULL);
    int x=12,y=10;
    AddCtrl(p,"STATIC","Core presence mask SMN[0x0115A870]:", x,y,250,20, IDC_K_MASK, SS_LEFT);
    AddCtrl(p,"STATIC","--", x+260,y,200,20, IDC_K_MASK, SS_LEFT);
    y+=34;
    AddCtrl(p,"BUTTON","Unlock 8 cores / 16 threads", x,y,220,28, IDC_K_UNLOCK, BS_PUSHBUTTON);
    AddCtrl(p,"STATIC","Result:", x,y+40,60,20, IDC_K_RESULT, SS_LEFT);
    AddCtrl(p,"STATIC","--", x+70,y+40,460,40, IDC_K_RESULT, SS_LEFT);
    return p;
}

static HWND BuildCuStatusPage(HWND parent)
{
    HWND p = CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_LEFT,
                             0,0,1,1,parent,NULL,GetModuleHandle(NULL),NULL);
    int x=12,y=10,lw=230,rw=220,rh=20;
    const struct { int id; const char* name; } rows[] = {
        { IDC_U_SPI,"SPI_PG 0x5C3C"},
        { IDC_U_CC,"CC_ARRAY 0x9C1C"},
        { IDC_U_RLC,"RLC_PG 0x3D64"},
        { IDC_U_WGP,"Active WGP (SMU 0x1E)"},
    };
    for (int i=0;i<4;i++){
        AddCtrl(p,"STATIC",rows[i].name, x,y,lw,rh, IDC_U_SPI, SS_LEFT);
        AddCtrl(p,"STATIC","--", x+lw+40,y,rw,rh, rows[i].id, SS_LEFT);
        y+=24;
    }
    y+=10;
    AddCtrl(p,"BUTTON","Read CU status", x,y,130,26, IDC_U_READ, BS_PUSHBUTTON);
    AddCtrl(p,"BUTTON","Attempt 40CU unlock", x+150,y,160,26, IDC_U_ATTEMPT, BS_PUSHBUTTON);
    AddCtrl(p,"STATIC","Result:", x,y+38,60,20, IDC_U_RESULT, SS_LEFT);
    AddCtrl(p,"STATIC","--", x+70,y+38,540,80, IDC_U_RESULT, SS_LEFT);
    return p;
}

static HWND BuildRegistersPage(HWND parent)
{
    HWND p = CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE|SS_LEFT,
                             0,0,1,1,parent,NULL,GetModuleHandle(NULL),NULL);
    int x=12,y=10;
    AddCtrl(p,"STATIC","BAR5 register offset (hex):", x,y,190,20, IDC_R_OFF, SS_LEFT);
    AddCtrl(p,"EDIT","3260", x+200,y,90,22, IDC_R_OFF, ES_AUTOHSCROLL|WS_BORDER);
    AddCtrl(p,"STATIC","Value (hex):", x+310,y,90,20, IDC_R_VAL, SS_LEFT);
    AddCtrl(p,"EDIT","0", x+400,y,90,22, IDC_R_VAL, ES_AUTOHSCROLL|WS_BORDER);
    AddCtrl(p,"BUTTON","Read", x+510,y,70,24, IDC_R_READ, BS_PUSHBUTTON);
    AddCtrl(p,"BUTTON","Write", x+590,y,70,24, IDC_R_WRITE, BS_PUSHBUTTON);
    AddCtrl(p,"STATIC","", x,y+26,420,20, IDC_R_RES, SS_LEFT);
    y+=60;
    AddCtrl(p,"STATIC","SMN address (hex):", x,y,150,20, IDC_S_OFF, SS_LEFT);
    AddCtrl(p,"EDIT","03B10000", x+160,y,100,22, IDC_S_OFF, ES_AUTOHSCROLL|WS_BORDER);
    AddCtrl(p,"STATIC","Value (hex):", x+280,y,90,20, IDC_S_VAL, SS_LEFT);
    AddCtrl(p,"EDIT","0", x+370,y,100,22, IDC_S_VAL, ES_AUTOHSCROLL|WS_BORDER);
    AddCtrl(p,"BUTTON","Read", x+490,y,70,24, IDC_S_READ, BS_PUSHBUTTON);
    AddCtrl(p,"BUTTON","Write", x+570,y,70,24, IDC_S_WRITE, BS_PUSHBUTTON);
    AddCtrl(p,"STATIC","", x,y+26,420,20, IDC_S_RES, SS_LEFT);
    return p;
}

static void TabSwitch(int idx)
{
    for (int i=0;i<6;i++){
        ShowWindow(g_page[i], i==idx ? SW_SHOW : SW_HIDE);
        if (i==idx){
            RECT rc; GetClientRect(g_hTab,&rc);
            SetWindowPos(g_page[i], NULL, 8,30, rc.right-16, rc.bottom-40,
                         SWP_NOZORDER);
        }
    }
}

static void Logf(const char *fmt, ...)
{
    char buf[4096]; va_list ap;
    va_start(ap,fmt); vsprintf_s(buf,sizeof(buf),fmt,ap); va_end(ap);
    int len = GetWindowTextLengthA(g_hLog);
    SendMessageA(g_hLog, EM_SETSEL, len, len);
    SendMessageA(g_hLog, EM_REPLACESEL, 0, (LPARAM)buf);
}

/* ------------------------------------------------------------------ */
/* Refresh monitor (also used by timer)                               */
/* ------------------------------------------------------------------ */
static void RefreshMonitor(void)
{
    if (g_h==INVALID_HANDLE_VALUE) return;

    uint32_t gpuId  = DrvR32(0x0000);
    uint32_t grbm   = DrvR32(0x3260);
    q0(0x02,0); uint32_t smuver=q0_arg();
    q0(0x03,0); uint32_t drvif =q0_arg();
    q0(0x37,0); uint32_t freq  =q0_arg();
    q0(0x38,0); uint32_t vid   =q0_arg();
    q0(0x1E,0); uint32_t wgp   =q0_arg();
    q0(0x3D,0); uint32_t feat  =q0_arg();
    q3(0x36,0); uint32_t cpumv =q3_arg();
    q3(0x37,0); uint32_t gpumv =q3_arg();
    uint32_t cores = smnR(0x0115A870);
    uint32_t tEdge = smnR(0x03B10000);
    uint32_t tJun  = smnR(0x03B10020);
    uint32_t tMem  = smnR(0x03B10028);
    uint32_t fanRpm= smnR(0x03B10064);
    uint32_t fanPwm= smnR(0x03B10068);

    HWND p=g_page[0];
    SetLbl(p,IDC_M_DRIVER,"%s",g_driverName);
    SetLbl(p,IDC_M_GPU_ID,"0x%08X",gpuId);
    SetLbl(p,IDC_M_GRBM,"0x%08X (GUI_ACT=%d IA=%d WD=%d)",grbm,(grbm>>31)&1,(grbm>>19)&1,(grbm>>18)&1);
    SetLbl(p,IDC_M_SMUVER,"v%u.%u.%u (if=%u)",(smuver>>16)&0xFF,(smuver>>8)&0xFF,smuver&0xFF,drvif);
    SetLbl(p,IDC_M_FREQ,"%u MHz",freq);
    SetLbl(p,IDC_M_VID,"%u",vid);
    SetLbl(p,IDC_M_MV,"%d mV",vid_to_mv((int)vid));
    SetLbl(p,IDC_M_WGP,"%u",wgp);
    SetLbl(p,IDC_M_FEAT,"0x%08X",feat);
    SetLbl(p,IDC_M_CORES,"mask 0x%02X (%u cores)",cores&0xFF, popcount32(cores&0xFF));
    SetLbl(p,IDC_M_TCDGE,"0x%02X",tEdge&0xFF);
    SetLbl(p,IDC_M_TJUNC,"0x%02X",tJun&0xFF);
    SetLbl(p,IDC_M_TMEM,"0x%02X",tMem&0xFF);
    SetLbl(p,IDC_M_FAN,"%u RPM / %u PWM",fanRpm,fanPwm);
    SetLbl(p,IDC_M_CPUMV,"%u mV",cpumv);
    SetLbl(p,IDC_M_GPUMV,"%u mV",gpumv);
}

/* ------------------------------------------------------------------ */
/* Action handlers                                                    */
/* ------------------------------------------------------------------ */
static void GovernorApply(HWND page)
{
    uint32_t target = GetEditU32(page, IDC_G_FREQ, 1500);
    uint32_t mv     = GetEditU32(page, IDC_G_MV,  900);
    uint32_t profile= GetEditU32(page, IDC_G_PROFILE, 3);
    int vid = mv_to_vid((int)mv);

    Logf("Governor change_freq(%u MHz, %u mV, VID=%d, profile=%u)\n", target, mv, vid, profile);
    int r=q3(0x8C,80);            Logf("  set_gpu_max_temp(80C)=%d\n",r);
    r=q0(0x3A,0);                 Logf("  unforce_gfx_freq=%d\n",r);
    r=q0(0x3C,0);                 Logf("  unforce_gfx_vid=%d\n",r);
    r=q3(0x1E,profile);           Logf("  set_perf_profile(%u)=%d\n",profile,r);
    if (r!=1){ Logf("  FAIL: profile\n"); return; }
    Sleep(50);
    r=q0(0x3B,(uint32_t)vid);     Logf("  force_gfx_vid(%d)=%d\n",vid,r);
    if (r!=1){ Logf("  FAIL: voltage\n"); return; }
    Sleep(50);
    r=q0(0x39,target);            Logf("  force_gfx_freq(%u)=%d\n",target,r);
    if (r!=1){ Logf("  FAIL: freq\n"); return; }
    Sleep(200);
    q0(0x37,0); uint32_t f2=q0_arg();
    q0(0x1E,0); uint32_t w2=q0_arg();
    Logf("  after: freq=%u MHz, active WGP=%u\n", f2, w2);
}

static void GovernorUnforce(HWND page)
{
    Logf("Governor restore:\n");
    Logf("  unforce_freq=%d unforce_vid=%d\n", q0(0x3A,0), q0(0x3C,0));
}

static void GovernorSetTemp(HWND page)
{
    Logf("set_gpu_max_temp(80C)=%d\n", q3(0x8C,80));
}

static void CpuOcApply(HWND page)
{
    uint32_t freq = GetEditU32(page,IDC_C_FREQ,3600);
    uint32_t vid  = GetEditU32(page,IDC_C_VID,1000);
    uint32_t temp = GetEditU32(page,IDC_C_TEMP,90);
    uint32_t kind = GetEditU32(page,IDC_C_KIND,0);

    if (freq<3000 || freq>4200){ Logf("CPU OC: freq %u outside 3000-4200\n",freq); return; }
    if (vid<900 || vid>1375){ Logf("CPU OC: vid %u outside 900-1375 mV\n",vid); return; }
    if (temp<70 || temp>90){ Logf("CPU OC: temp %u outside 70-90\n",temp); return; }

    Logf("CPU OC temporary: freq=%u MHz, VID=%u mV, temp=%u, kind=%u\n", freq, vid, temp, kind);
    int r=q3(0x20,temp);            Logf("  set_max_temp(%u)=%d\n",temp,r);
    Sleep(30);
    uint32_t arg = (kind<<16)|vid;
    r=q3(0x0F,arg);                Logf("  set_cpu_gpu_vid(kind=%u vid=%u)=%d\n",kind,vid,r);
    Sleep(30);
    int ok=0;
    for (uint32_t c=0;c<8;c++){
        r=q3(0x25,(c<<16)|freq);   Logf("  set_oc_clk(core%u=%uMHz)=%d\n",c,freq,r);
        if (r==1) ok++;
    }
    Logf("  cores accepted: %d/8\n", ok);
    SetLbl(page,IDC_C_INFO,"freq %u MHz, VID %u mV, temp %u (cores %d/8 OK)",freq,vid,temp,ok);
}

static void CoreUnlock(HWND page)
{
    uint32_t before = smnR(0x0115A870);
    SetLbl(page,IDC_K_MASK,"before 0x%02X",before&0xFF);
    Logf("Core unlock: mask before=0x%02X (%u cores)\n",before&0xFF,popcount32(before&0xFF));

    if ((before&0xFF)==0xFF){ Logf("  already 0xFF, nothing to do\n"); return; }
    if ((before&0xFF)!=0x77){ Logf("  unexpected mask 0x%02X, refusing\n",before&0xFF); return; }

    int r=q3_wait_result(0x98, 0x0115A870, 2000);
    uint32_t after = 0;
    Sleep(300);
    after = smnR(0x0115A870);
    SetLbl(page,IDC_K_MASK,"after 0x%02X",after&0xFF);
    Logf("  msg 0x98 status=%d, mask after=0x%02X\n", r, after&0xFF);
    if ((after&0xFF)==0xFF)
        Logf("  OK! Reboot to bring up 8 cores / 16 threads. Cold power-off may revert.\n");
    else
        Logf("  unlock did not stick (mask 0x%02X).\n", after&0xFF);
}

static void CuStatus(HWND page)
{
    uint32_t spi = DrvR32(0x5C3C);
    uint32_t cc  = DrvR32(0x9C1C);
    uint32_t rlc = DrvR32(0x3D64);
    q0(0x1E,0); uint32_t wgp = q0_arg();
    SetLbl(page,IDC_U_SPI,"0x%08X",spi);
    SetLbl(page,IDC_U_CC,"0x%08X",cc);
    SetLbl(page,IDC_U_RLC,"0x%08X",rlc);
    SetLbl(page,IDC_U_WGP,"%u",wgp);
    SetLbl(page,IDC_U_RESULT,"SPI_PG=0x%X CC=0x%X RLC=0x%X activeWGP=%u",spi,cc,rlc,wgp);
    Logf("CU: SPI_PG=0x%08X CC_ARRAY=0x%08X RLC_PG=0x%08X activeWGP=%u\n",spi,cc,rlc,wgp);
}

static void CuAttempt(HWND page)
{
    Logf("40CU attempt (duggasco values: CC=0, SPI=0x1F, RLC=0x1F):\n");
    DrvW32(0x5C3C,0x1F);
    DrvW32(0x9C1C,0);
    DrvW32(0x3D64,0x1F);
    Sleep(50);
    uint32_t spi=DrvR32(0x5C3C), cc=DrvR32(0x9C1C), rlc=DrvR32(0x3D64);
    q0(0x1E,0); uint32_t wgp=q0_arg();
    SetLbl(page,IDC_U_SPI,"0x%08X",spi);
    SetLbl(page,IDC_U_CC,"0x%08X",cc);
    SetLbl(page,IDC_U_RLC,"0x%08X",rlc);
    SetLbl(page,IDC_U_WGP,"%u",wgp);
    Logf("  after: SPI_PG=0x%08X CC=0x%08X RLC=0x%08X activeWGP=%u\n",spi,cc,rlc,wgp);
    SetLbl(page,IDC_U_RESULT,"after attempt: SPI=0x%X CC=0x%X RLC=0x%X WGP=%u%s",
        spi,cc,rlc,wgp,
        (spi==0x1F && wgp>0)?"  => UNLOCKED!":
        (spi==0x1F)?"  => SPI_PG writable (WGP still 0)":
        "  => SPI_PG locked (expected on Windows; use EFI boot unlock)");
}

/* ------------------------------------------------------------------ */
/* Main window + message loop                                         */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg){
    case WM_CREATE: {
        InitCommonControls();
        HWND tc = CreateWindowExA(0,WC_TABCONTROL,"",
            WS_CHILD|WS_VISIBLE|TCS_FIXEDWIDTH,
            0,0,0,0, hwnd,(HMENU)(INT_PTR)IDC_TAB,GetModuleHandle(NULL),NULL);
        SendMessageA(tc,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        g_hTab = tc;
        for (int i=0;i<6;i++){
            TCITEMA ti; ZeroMemory(&ti,sizeof(ti));
            ti.mask=TCIF_TEXT; ti.pszText=(LPSTR)g_tabNames[i];
            SendMessageA(tc,TCM_INSERTITEM,i,(LPARAM)&ti);
        }
        g_page[0]=BuildMonitorPage(hwnd);
        g_page[1]=BuildGovernorPage(hwnd);
        g_page[2]=BuildCpuOcPage(hwnd);
        g_page[3]=BuildCoreUnlockPage(hwnd);
        g_page[4]=BuildCuStatusPage(hwnd);
        g_page[5]=BuildRegistersPage(hwnd);

        g_hLog = CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,
            0,0,0,0, hwnd,(HMENU)(INT_PTR)IDC_LOG,GetModuleHandle(NULL),NULL);
        SendMessageA(g_hLog,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);

        if (!BackendOpen()){
            Logf("Could not open AMDBC250DreamV43 or AMDBC250Reg. Start kernel service first.\n");
        } else {
            Logf("Connected: %s\n", g_driverName);
            RefreshMonitor();
        }
        Logf("BC-250 Control Center ready.\n");
        SetTimer(hwnd, 1, 1000, NULL);
        return 0;
    }
    case WM_SIZE: {
        RECT rc; GetClientRect(hwnd,&rc);
        int tabH = 300;
        int logH = 120;
        SetWindowPos(g_hTab, NULL, 0,0, rc.right, tabH, SWP_NOZORDER);
        SetWindowPos(g_hLog, NULL, 0, rc.bottom-logH, rc.right, logH, SWP_NOZORDER);
        int sel = (int)SendMessageA(g_hTab,TCM_GETCURSEL,0,0);
        if (sel<0) sel=0;
        TabSwitch(sel);
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *p=(NMHDR*)lParam;
        if (p->idFrom==IDC_TAB && p->code==TCN_SELCHANGE){
            TabSwitch((int)SendMessageA(g_hTab,TCM_GETCURSEL,0,0));
        }
        return 0;
    }
    case WM_COMMAND: {
        int id=LOWORD(wParam);
        switch (id){
        case IDC_M_REFRESH: RefreshMonitor(); break;
        case IDC_M_AUTO: g_autoRefresh = (SendMessageA((HWND)lParam,BM_GETCHECK,0,0)==BST_CHECKED); break;
        case IDC_G_APPLY:  GovernorApply(g_page[1]); break;
        case IDC_G_UNFORCE:GovernorUnforce(g_page[1]); break;
        case IDC_G_TEMP:   GovernorSetTemp(g_page[1]); break;
        case IDC_C_APPLY:  CpuOcApply(g_page[2]); break;
        case IDC_K_UNLOCK: CoreUnlock(g_page[3]); break;
        case IDC_U_READ:   CuStatus(g_page[4]); break;
        case IDC_U_ATTEMPT:CuAttempt(g_page[4]); break;
        case IDC_R_READ: {
            uint32_t off=GetEditU32(g_page[5],IDC_R_OFF,0);
            uint32_t v=DrvR32(off);
            SetEdit(g_page[5],IDC_R_VAL,"0x%X",v);
            SetLbl(g_page[5],IDC_R_RES,"BAR5[0x%X] = 0x%08X",off,v);
            break;
        }
        case IDC_R_WRITE: {
            uint32_t off=GetEditU32(g_page[5],IDC_R_OFF,0);
            uint32_t val=GetEditU32(g_page[5],IDC_R_VAL,0);
            DrvW32(off,val);
            uint32_t rb=DrvR32(off);
            SetLbl(g_page[5],IDC_R_RES,"WROTE BAR5[0x%X]=0x%08X readback=0x%08X%s",
                   off,val,rb,(rb==val)?"":" (did not stick)");
            break;
        }
        case IDC_S_READ: {
            uint32_t a=GetEditU32(g_page[5],IDC_S_OFF,0);
            uint32_t v=smnR(a);
            SetEdit(g_page[5],IDC_S_VAL,"0x%X",v);
            SetLbl(g_page[5],IDC_S_RES,"SMN[0x%08X] = 0x%08X",a,v);
            break;
        }
        case IDC_S_WRITE: {
            uint32_t a=GetEditU32(g_page[5],IDC_S_OFF,0);
            uint32_t val=GetEditU32(g_page[5],IDC_S_VAL,0);
            smnW(a,val);
            uint32_t rb=smnR(a);
            SetLbl(g_page[5],IDC_S_RES,"WROTE SMN[0x%08X]=0x%08X readback=0x%08X%s",
                   a,val,rb,(rb==val)?"":" (did not stick)");
            break;
        }
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam==1){ if(g_autoRefresh){ /* only refresh when a driver is open */ if(g_h!=INVALID_HANDLE_VALUE) RefreshMonitor(); } }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd,1);
        BackendClose();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd,msg,wParam,lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hPrev;(void)lpCmd;
    WNDCLASSA wc;
    ZeroMemory(&wc,sizeof(wc));
    wc.style=CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc=WndProc;
    wc.hInstance=hInst;
    wc.hIcon=LoadIcon(NULL,IDI_APPLICATION);
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
    wc.lpszClassName="BC250CC";
    RegisterClassA(&wc);

    HWND hwnd=CreateWindowExA(0,"BC250CC","BC-250 Control Center",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,CW_USEDEFAULT,940,640,
        NULL,NULL,hInst,NULL);
    if(!hwnd){ MessageBoxA(NULL,"CreateWindow failed","BC-250 CC",MB_OK); return 1; }
    ShowWindow(hwnd,nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg,NULL,0,0)>0){
        if (!IsDialogMessageA(g_page[0],&msg) && !IsDialogMessageA(g_page[1],&msg) &&
            !IsDialogMessageA(g_page[2],&msg) && !IsDialogMessageA(g_page[3],&msg) &&
            !IsDialogMessageA(g_page[4],&msg) && !IsDialogMessageA(g_page[5],&msg)){
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    return (int)msg.wParam;
}