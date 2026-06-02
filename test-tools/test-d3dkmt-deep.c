#include <windows.h>
#include <stdio.h>
#include <d3dkmthk.h>

int main(void) {
    FILE *g;
    fopen_s(&g, "C:\\AMD-BC-250\\AMD-BC-250-Windows-Driver-main\\output\\d3dkmt-deep.log", "w");
    if (!g) return 1;
    fprintf(g, "STEP 1: before main\n"); fflush(g);

    fprintf(g, "STEP 2: EnumAdapters\n"); fflush(g);
    D3DKMT_ENUMADAPTERS ea = {0};
    NTSTATUS st = D3DKMTEnumAdapters(&ea);
    fprintf(g, "  Status=0x%08X Count=%u\n", st, ea.NumAdapters); fflush(g);
    for (UINT i = 0; i < ea.NumAdapters; i++) {
        fprintf(g, "  [%u] h=0x%08X Luid=%u.%d Src=%u\n",
            i, ea.Adapters[i].hAdapter,
            ea.Adapters[i].AdapterLuid.HighPart,
            ea.Adapters[i].AdapterLuid.LowPart,
            ea.Adapters[i].NumOfSources);
        fflush(g);
    }

    fprintf(g, "STEP 3: OpenAdapterFromHdc\n"); fflush(g);
    HDC hdc = GetDC(NULL);
    D3DKMT_OPENADAPTERFROMHDC oah = {0};
    oah.hDc = hdc;
    st = D3DKMTOpenAdapterFromHdc(&oah);
    fprintf(g, "  Status=0x%08X h=0x%08X Luid=%u.%d\n", st, oah.hAdapter, oah.AdapterLuid.HighPart, oah.AdapterLuid.LowPart);
    fflush(g);
    D3DKMT_HANDLE hAdapter = oah.hAdapter;
    LUID adapterLuid = oah.AdapterLuid;
    ReleaseDC(NULL, hdc);
    if (!hAdapter) { fprintf(g, "FATAL: no adapter\n"); fclose(g); return 1; }

    fprintf(g, "STEP 4: CreateDevice\n"); fflush(g);
    {
        D3DKMT_CREATEDEVICE cd = {0};
        cd.hAdapter = hAdapter;
        st = D3DKMTCreateDevice(&cd);
        fprintf(g, "  Status=0x%08X hDevice=0x%08X\n", st, cd.hDevice); fflush(g);
        if (st == 0) {
            D3DKMT_DESTROYDEVICE dd = {0}; dd.hDevice = cd.hDevice;
            D3DKMTDestroyDevice(&dd);
        }
    }

    fprintf(g, "STEP 5: Segment Stats\n"); fflush(g);
    for (int seg = 0; seg < 4; seg++) {
        D3DKMT_QUERYSTATISTICS qs = {0};
        qs.Type = D3DKMT_QUERYSTATISTICS_SEGMENT;
        qs.AdapterLuid = adapterLuid;
        qs.QuerySegment.SegmentId = seg;
        st = D3DKMTQueryStatistics(&qs);
        if (st != 0) { fprintf(g, "  Seg[%d]: 0x%08X\n", seg, st); fflush(g); continue; }
        D3DKMT_QUERYSTATISTICS_SEGMENT_INFORMATION *si = &qs.QueryResult.SegmentInformation;
        fprintf(g, "  Seg[%d]: Commit=%lluMB Committed=%lluMB Resident=%lluMB Aperture=%u\n",
            seg, si->CommitLimit/(1024*1024), si->BytesCommitted/(1024*1024),
            si->BytesResident/(1024*1024), si->Aperture);
        fprintf(g, "    Evicted=%lluMB AllocsCommitted=%u AllocsResident=%u SysMemEnd=0x%llX\n",
            si->Memory.TotalBytesEvicted/(1024*1024), si->Memory.AllocsCommitted,
            si->Memory.AllocsResident, si->SystemMemoryEndAddress);
        fflush(g);
    }

    fprintf(g, "STEP 6: Node Stats\n"); fflush(g);
    for (int node = 0; node < 16; node++) {
        D3DKMT_QUERYSTATISTICS qs = {0};
        qs.Type = D3DKMT_QUERYSTATISTICS_NODE;
        qs.AdapterLuid = adapterLuid;
        qs.QueryNode.NodeId = node;
        st = D3DKMTQueryStatistics(&qs);
        if (st != 0) continue;
        D3DKMT_QUERYSTATISTICS_NODE_INFORMATION *ni = &qs.QueryResult.NodeInformation;
        fprintf(g, "  Node[%d]: CtxSwitch=%u Running=%lld Preempted=%lld\n", node,
            ni->GlobalInformation.ContextSwitch,
            ni->GlobalInformation.RunningTime.QuadPart, 0LL);
        fflush(g);
    }

    fprintf(g, "STEP 7: VidPnSource Stats\n"); fflush(g);
    {
        D3DKMT_QUERYSTATISTICS qs = {0};
        qs.Type = D3DKMT_QUERYSTATISTICS_VIDPNSOURCE;
        qs.AdapterLuid = adapterLuid;
        qs.QueryVidPnSource.VidPnSourceId = 0;
        st = D3DKMTQueryStatistics(&qs);
        fprintf(g, "  VPnSrc[0]: Status=0x%08X\n", st); fflush(g);
        if (st == 0) {
            fprintf(g, "    Frame=%u Cancel=%u Queued=%u\n",
                qs.QueryResult.VidPnSourceInformation.GlobalInformation.Frame,
                qs.QueryResult.VidPnSourceInformation.GlobalInformation.CancelledFrame,
                qs.QueryResult.VidPnSourceInformation.GlobalInformation.QueuedPresent);
            fflush(g);
        }
    }

    fprintf(g, "STEP 8: QueryAdapterInfo (all types)\n"); fflush(g);
    for (int type = 0; type <= 80; type++) {
        UCHAR buf[1024] = {0};
        D3DKMT_QUERYADAPTERINFO qa = {0};
        qa.hAdapter = hAdapter;
        qa.Type = (KMTQUERYADAPTERINFOTYPE)type;
        qa.pPrivateDriverData = buf;
        qa.PrivateDriverDataSize = sizeof(buf);
        st = D3DKMTQueryAdapterInfo(&qa);
        if (st == 0) {
            fprintf(g, "  Type[%2d]: ", type);
            UINT64 *v = (UINT64*)buf;
            for (int j = 0; j < 4; j++) { if (v[j] != 0) fprintf(g, "[%d]=%llX ", j, v[j]); }
            fprintf(g, "\n"); fflush(g);
        }
    }

    fprintf(g, "STEP 9: CloseAdapter\n"); fflush(g);
    {
        D3DKMT_CLOSEADAPTER ca = {0};
        ca.hAdapter = hAdapter;
        st = D3DKMTCloseAdapter(&ca);
        fprintf(g, "  Status=0x%08X\n", st); fflush(g);
    }

    fprintf(g, "STEP 10: DONE\n"); fflush(g);
    fclose(g);
    return 0;
}
