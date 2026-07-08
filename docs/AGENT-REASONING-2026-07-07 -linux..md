Search the large file at C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4 for register definitions related to:
1. CP_HQD (any mmCP_HQD_* and regCP_HQD_* definitions with their mm values and BASE_IDX)
2. CP_MQD_BASE_ADDR (mm and reg versions)
3. GRBM_GFX_INDEX 
4. KIQ (mmKIQ_* or regKIQ_*)

Use grep to find all matches. Report back:
- The exact mm or reg number for each register
- Its BASE_IDX value
- The full line content including any comments
The user wants me to search a specific large file for register definitions related to CP_HQD, CP_MQD_BASE_ADDR, GRBM_GFX_INDEX, and KIQ. Let me first check what this file is and then grep for the relevant patterns.

$ Get-Item "C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4" | Select-Object Length, LastWriteTime


 Length LastWriteTime       
 ------ -------------       
1199658 7/7/2026 10:14:45 AM


The grep for KIQ returned no results. Let me search more specifically for KIQ in the file. Also, I need to get the full lines for CP_HQD_PQ_CONTROL, CP_MQD_BASE_ADDR, and other key registers. Let me also search for regCP_HQD_* definitions and look for KIQ.

The regCP_HQD_* and regGRBM_GFX_* searches returned no results - so there are no reg* style register definitions in this file, only mm* style. Let me also search for GC_BASE, any comments mentioning BASE_IDX, and look at the header structure of this file to understand what IP block it's from.

Let me also check what the GC_BASE values are and look for register offset calculations. The key thing the user wants is:

CP_HQD mm values and BASE_IDX
CP_MQD_BASE_ADDR mm and BASE_IDX
GRBM_GFX_INDEX mm and BASE_IDX
KIQ registers
Let me search for KIQ more broadly - maybe the grep is case sensitive or the format is different.

$ Select-String -Pattern "KIQ|kiq" -Path "C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4" -SimpleMatch | Select-Object -First 20

(no output)
So this is the Linux gc_10_1_0_offset.h header file. No KIQ registers found in it either by grep or Select-String. Let me search for what the KIQ registers might be called - in the AMD headers they might be different register names. Let me also look for PQ_WPTR and PQ_RPTR specifically, and check if there are any other interesting CP registers.

Let me also look for CP_HQD_PERSISTENT_STATE which I saw at line 5311, and CP_HQD_PQ_CONTROL at line 5335. Let me look at what mm values those are.

From the grep results I already have:

mmCP_MQD_BASE_ADDR = 0x1fa9, BASE_IDX = 0
mmCP_MQD_BASE_ADDR_HI = 0x1faa, BASE_IDX = 0
mmCP_HQD_ACTIVE = 0x1fab, BASE_IDX = 0
mmCP_HQD_VMID = 0x1fac, BASE_IDX = 0
mmCP_HQD_PERSISTENT_STATE = 0x1fad, BASE_IDX = 0
mmCP_HQD_PQ_BASE = 0x1fb1, BASE_IDX = 0
mmCP_HQD_PQ_RPTR = 0x1fb3, BASE_IDX = 0
mmCP_HQD_PQ_CONTROL = 0x1fba, BASE_IDX = 0
mmCP_HQD_PQ_WPTR_LO = 0x1fdf, BASE_IDX = 0
mmCP_HQD_PQ_WPTR_HI = 0x1fe0, BASE_IDX = 0
mmGRBM_GFX_INDEX = 0x2200, BASE_IDX = 1
Now I need to get the full lines including comments for each. Let me read the specific ranges more carefully.

The file is very large (11375 lines, 1.2MB). Let me read the key sections.

Now let me also check for any address block comments before the CP_HQD section, to see which base address it belongs to. Let me also search for KIQ-related registers - in gc_10_1_0_offset.h they might not use "KIQ" in the name. Let me search for it differently, maybe with CP_ME or CP_MEC related registers that could be KIQ.

Actually, the grep for KIQ didn't find anything, so this file doesn't contain KIQ register definitions. KIQ registers would be in gc_10_1_0_offset.h typically as mmCP_MEC_ME1_* or mmCP_MEC_F32_* entries.

Let me also look for what address block comment is before the CP_HQD region.

$ Select-String -Pattern "addressBlock.*cp|addressBlock.*CP" -Path "C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4" -SimpleMatch

(no output)
$ Select-String -Pattern "addressBlock" -Path "C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4" -SimpleMatch | Select-Object -First 30


C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:29:// addressBlock: gc_sdma0_sdma0dec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:1036:// addressBlock: 
gc_sdma1_sdma1dec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2035:// addressBlock: gc_grbmdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2125:// addressBlock: gc_cpdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2273:// addressBlock: gc_padec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2425:// addressBlock: gc_sqdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2577:// addressBlock: gc_shsdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2729:// addressBlock: gc_tpdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2757:// addressBlock: gc_gdsdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2787:// addressBlock: gc_rbdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2987:// addressBlock: gc_gceadec2
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:3037:// addressBlock: gc_spipdec2
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:3047:// addressBlock: gc_gceadec3
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:3073:// addressBlock: gc_rmi_rmidec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:3137:// addressBlock: gc_pmmdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:3155:// addressBlock: gc_utcl1dec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:3167:// addressBlock: gc_gcatcl2dec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:3195:// addressBlock: gc_gcvml2pfdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:3269:// addressBlock: gc_gcvml2vcdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:3679:// addressBlock: 
gc_gcvmsharedpfdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:3723:// addressBlock: 
gc_gcvmsharedvcdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:3743:// addressBlock: gc_gceadec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4005:// addressBlock: gc_tcdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4027:// addressBlock: gc_shdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4683:// addressBlock: gc_cppdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:5171:// addressBlock: gc_spipdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:5289:// addressBlock: gc_cpphqddec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:5437:// addressBlock: gc_didtdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:5447:// addressBlock: gc_gccacdec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:5499:// addressBlock: gc_tcpdec


Now I can see the structure. The CP_HQD registers are in address block gc_cpphqddec starting at line 5289. Let me check what's at line 5289 to see if there's a base address comment. Also, let me look at the gc_cpdec section (line 2125), and the gc_cppdec section (line 4683) to see if there are KIQ-related registers there. I need to find KIQ_BASE, KIQ_SIZE, etc.

In our AGENTS.md we know that KIQ_BASE and KIQ_SIZE are hardwired to 0 - they would have a different naming convention in this header. Let me search for CP_MEC or CP_ME related registers that might be KIQ.

$ Select-String -Pattern "CP_MEC|CP_ME[^_]|MEC_ME|MEC_HPD|MEC_CNTL|KIQ" -Path "C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4" | Select-Object -First 40


C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2143:#define mmCP_MEC_CNTL           
                                                                       0x0e2d
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2144:#define mmCP_MEC_CNTL_BASE_IDX  
                                                                       0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2145:#define 
mmCP_MEC_ME1_HEADER_DUMP                                                                       0x0e2e
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2146:#define 
mmCP_MEC_ME1_HEADER_DUMP_BASE_IDX                                                              0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2147:#define 
mmCP_MEC_ME2_HEADER_DUMP                                                                       0x0e2f
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2148:#define 
mmCP_MEC_ME2_HEADER_DUMP_BASE_IDX                                                              0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2193:#define mmCP_MEC1_INSTR_PNTR    
                                                                       0x0f48
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2194:#define 
mmCP_MEC1_INSTR_PNTR_BASE_IDX                                                                  0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2195:#define mmCP_MEC2_INSTR_PNTR    
                                                                       0x0f49
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2196:#define 
mmCP_MEC2_INSTR_PNTR_BASE_IDX                                                                  0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2207:#define mmCP_MEQ_STQ_THRESHOLD  
                                                                       0x0f5d
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2208:#define 
mmCP_MEQ_STQ_THRESHOLD_BASE_IDX                                                                0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2229:#define mmCP_MEQ_THRESHOLDS     
                                                                       0x0f79
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2230:#define 
mmCP_MEQ_THRESHOLDS_BASE_IDX                                                                   0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2237:#define mmCP_MEQ_AVAIL          
                                                                       0x0f7d
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2238:#define mmCP_MEQ_AVAIL_BASE_IDX 
                                                                       0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2253:#define mmCP_MEQ_STAT           
                                                                       0x0f85
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:2254:#define mmCP_MEQ_STAT_BASE_IDX  
                                                                       0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4737:#define 
mmCP_ME0_PIPE_PRIORITY_CNTS                                                                    0x1dec
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4738:#define 
mmCP_ME0_PIPE_PRIORITY_CNTS_BASE_IDX                                                           0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4741:#define mmCP_ME0_PIPE0_PRIORITY 
                                                                       0x1ded
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4742:#define 
mmCP_ME0_PIPE0_PRIORITY_BASE_IDX                                                               0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4745:#define mmCP_ME0_PIPE1_PRIORITY 
                                                                       0x1dee
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4746:#define 
mmCP_ME0_PIPE1_PRIORITY_BASE_IDX                                                               0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4749:#define mmCP_ME0_PIPE2_PRIORITY 
                                                                       0x1def
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4750:#define 
mmCP_ME0_PIPE2_PRIORITY_BASE_IDX                                                               0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4757:#define mmCP_ME0_PIPE0_VMID     
                                                                       0x1df2
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4758:#define 
mmCP_ME0_PIPE0_VMID_BASE_IDX                                                                   0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4759:#define mmCP_ME0_PIPE1_VMID     
                                                                       0x1df3
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4760:#define 
mmCP_ME0_PIPE1_VMID_BASE_IDX                                                                   0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4781:#define 
mmCP_MEC_DOORBELL_RANGE_LOWER                                                                  0x1dfc
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4782:#define 
mmCP_MEC_DOORBELL_RANGE_LOWER_BASE_IDX                                                         0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4783:#define 
mmCP_MEC_DOORBELL_RANGE_UPPER                                                                  0x1dfd
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4784:#define 
mmCP_MEC_DOORBELL_RANGE_UPPER_BASE_IDX                                                         0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4821:#define mmCP_MEM_SLP_CNTL       
                                                                       0x1e19
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4822:#define 
mmCP_MEM_SLP_CNTL_BASE_IDX                                                                     0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4841:#define mmCP_ME1_PIPE0_INT_CNTL 
                                                                       0x1e25
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4842:#define 
mmCP_ME1_PIPE0_INT_CNTL_BASE_IDX                                                               0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4843:#define mmCP_ME1_PIPE1_INT_CNTL 
                                                                       0x1e26
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4844:#define 
mmCP_ME1_PIPE1_INT_CNTL_BASE_IDX                                                               0


Now let me also look at the gc_cppdec section (line 4683) to find KIQ-related registers, and check what the addressBlock: gc_cppdec contains. Let me also look at the cpphqddec base address mapping comment which showed base address: 0xc800.

Key findings so far:

The file is Linux gc_10_1_0_offset.h
gc_cpdec section base address: 0x8200
gc_cppdec section starts at line 4683
gc_cpphqddec section base address: 0xc800, starts at line 5289
Let me look at cppdec section for KIQ registers.

I don't see KIQ-specific registers in this file. KIQ registers (like KIQ_BASE, KIQ_SIZE, etc.) might be in a different header file, or they might be named differently in gc_10_1_0_offset.h. In some AMD GPU headers, KIQ registers are named mmCP_MEC_ME1_* or they might be in a different address block.

Let me search for more patterns that could match KIQ-related registers. In the Linux kernel driver for Navi10/BC-250, KIQ registers might be:

mmCP_MEC_F32_*
Or in the range after mmCP_GFX_HQD_*
Let me also check what's in the address range after the cpphqddec section (line 5425+). Also, let me search for F32 which is the KIQ queue prefix.

$ Select-String -Pattern "F32|KIQ|ME1_PIPE4|ME1_PIPE5|ME1_PIPE6|ME1_PIPE7" -Path "C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4" | Select-Object -First 30


C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:73:#define mmSDMA0_F32_CNTL          
                                                                     0x002a
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:74:#define mmSDMA0_F32_CNTL_BASE_IDX 
                                                                     0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:153:#define mmSDMA0_F32_COUNTER      
                                                                      0x0055
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:154:#define 
mmSDMA0_F32_COUNTER_BASE_IDX                                                                   0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:1080:#define mmSDMA1_F32_CNTL        
                                                                       0x062a
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:1081:#define 
mmSDMA1_F32_CNTL_BASE_IDX                                                                      0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:1152:#define mmSDMA1_F32_COUNTER     
                                                                       0x0655
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:1153:#define 
mmSDMA1_F32_COUNTER_BASE_IDX                                                                   0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4947:#define mmCP_MEC1_F32_INT_DIS   
                                                                       0x1e5d
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4948:#define 
mmCP_MEC1_F32_INT_DIS_BASE_IDX                                                                 0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4949:#define mmCP_MEC2_F32_INT_DIS   
                                                                       0x1e5e
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:4950:#define 
mmCP_MEC2_F32_INT_DIS_BASE_IDX                                                                 0
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:9295:#define mmRLC_F32_UCODE_VERSION 
                                                                       0x4c03
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:9296:#define 
mmRLC_F32_UCODE_VERSION_BASE_IDX                                                               1
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:10423:#define mmRLC_GPU_IOV_F32_CNTL 
                                                                        0x5b46
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:10424:#define 
mmRLC_GPU_IOV_F32_CNTL_BASE_IDX                                                                1
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:10425:#define 
mmRLC_GPU_IOV_F32_RESET                                                                        0x5b47
C:\Users\Keshas\.local\share\opencode\tool-output\tool_f3ba4d15f0016BU7NqwovW23E4:10426:#define 
mmRLC_GPU_IOV_F32_RESET_BASE_IDX                                                               1


No KIQ-specific registers found in this file. The KIQ registers (KIQ_BASE, KIQ_SIZE, etc.) are likely defined in a different header file - possibly nbio_2_1_0_offset.h or a different GC header. These are ring buffer base/size registers for the Kernel Interface Queue, which are in the MEC register space but might be named differently in the Linux header.

Let me check if there's a file in the repo that defines KIQ registers, or if we need to look at the hw.h file used by the driver.

So the KIQ registers are in a different header (the hw.h from this repo), not in the Linux gc_10_1_0_offset.h. In the Linux headers, KIQ registers are likely in a different file.

Let me now also look at the section around the gc_cppdec that continues, maybe there are more CP_MEC registers. But the key question here is about the mm values from gc_10_1_0_offset.h.

Let me now look at what the remaining CP registers are in the cppdec section. Let me also check for the gc_cpphqddec base address 0xc800. For the hw.h, the mapping formula is:

If BASE_IDX = 0: BAR5 = GC_BASE(0x1260) + mm * 4
If BASE_IDX = 1: BAR5 = GC_BASE(0x1260) + 0xA000 + mm * 4
Let me verify:

GC_BASE = 0x1260
For BASE_IDX = 0: BAR5 = 0x1260 + mm * 4
For BASE_IDX = 1: BAR5 = 0x1260 + 0xA000 + mm * 4 = 0xB260 + mm * 4
Now let me compute the BAR5 offsets for the key registers and compare with what hw.h uses:

CP_MQD_BASE_ADDR: mm = 0x1fa9, BASE_IDX = 0 → BAR5 = 0x1260 + 0x1fa9 * 4 = 0x1260 + 0x7EA4 = 0x9104 ✅ (matches hw.h AMDBC250_REG_CP_MQD_BASE_ADDR = 0x9104)

CP_MQD_BASE_ADDR_HI: mm = 0x1faa, BASE_IDX = 0 → BAR5 = 0x1260 + 0x1faa * 4 = 0x1260 + 0x7EA8 = 0x9108

CP_HQD_ACTIVE: mm = 0x1fab, BASE_IDX = 0 → BAR5 = 0x1260 + 0x1fab * 4 = 0x1260 + 0x7EAC = 0x910C ✅ (matches AGENTS.md: 0x910C)

CP_HQD_VMID: mm = 0x1fac, BASE_IDX = 0 → BAR5 = 0x1260 + 0x1fac * 4 = 0x1260 + 0x7EB0 = 0x9110

CP_HQD_PERSISTENT_STATE: mm = 0x1fad, BASE_IDX = 0 → BAR5 = 0x1260 + 0x1fad * 4 = 0x1260 + 0x7EB4 = 0x9114

CP_HQD_PQ_BASE: mm = 0x1fb1, BASE_IDX = 0 → BAR5 = 0x1260 + 0x1fb1 * 4 = 0x1260 + 0x7EC4 = 0x9124 ✅ (matches AGENTS.md: 0x9124)

CP_HQD_PQ_RPTR: mm = 0x1fb3, BASE_IDX = 0 → BAR5 = 0x1260 + 0x1fb3 * 4 = 0x1260 + 0x7ECC = 0x912C ✅ (matches AGENTS.md: 0x912C)

CP_HQD_PQ_CONTROL: mm = 0x1fba, BASE_IDX = 0 → BAR5 = 0x1260 + 0x1fba * 4 = 0x1260 + 0x7EE8 = 0x90F0 (WAIT! Earlier the AGENTS.md says 0x90F0, but let me check: 0x1fba * 4 = 0x7EE8, + 0x1260 = 0x90F0)

Hmm, the AGENTS.md uses the term "SEG1" for some registers. Let me check the hw.h to see what offsets are actually used:

Let me also look at the hw.h for the specific register definitions.

Actually wait - let me re-check CP_HQD_PQ_CONTROL. The address block comment is gc_cpphqddec with base address: 0xc800. This means the Linux driver computes the BAR5 offset as:

For registers in address block gc_cpphqddec: BAR5 = GC_BASE + (mm_instance_offset - 0xc800) = 0x1260 + (0x1fba * 4 - 0xc800*4)... Hmm, that doesn't make sense.
Actually the Linux driver uses a different scheme. The mm values in these headers are just indices within the address block. The actual BAR5 address is computed as:

For GC IP: BAR5 = GC_BASE + mm * 4 (when BASE_IDX=0) or GC_BASE + 0xA000 + mm * 4 (when BASE_IDX=1)
So for CP_HQD_PQ_CONTROL: mm=0x1fba, BASE_IDX=0 → BAR5 = 0x1260 + 0x1fba*4 = 0x1260 + 0x7EE8 = 0x9148

Wait, but 0x1fba in decimal is 8122. 8122 * 4 = 32488 = 0x7EE8. 0x1260 + 0x7EE8 = 0x9148.

But earlier hw.h shows AMDBC250_REG_CP_HQD_PQ_CONTROL at a different offset. Let me check.

Actually from the AGENTS.md:

| CP_HQD_PQ_CONTROL | 0x1fba | 0xDAFC | 0x9148 |
So BAR5 = 0x9148. That's 0x1260 + 0x1fba*4.

Now GRBM_GFX_INDEX: mm=0x2200, BASE_IDX=1 → BAR5 = 0x1260 + 0xA000 + 0x2200*4 = 0xB260 + 0x8800 = 0x3A60

Wait, that seems wrong. Let me recompute: 0x2200 * 4 = 0x8800. 0xB260 + 0x8800 = 0x13A60. That's too big for BAR5 (which is only 512KB = 0x80000).

Let me check the hw.h to see what formula they actually use for BASE_IDX=1 registers.

Actually, wait. Looking at the AGENTS.md more carefully:

mmGRBM_GFX_CNTL (0x2022) was probed at WRONG address. CORRECT is 0x4968 (mm=0x0dc2, GC_BASE + mm*4).
So 0x0dc2 * 4 = 0x3708, + 0x1260 = 0x4968. That matches the GRBM_GFX_CNTL register.

But mmGRBM_GFX_INDEX is 0x2200, BASE_IDX=1. In hw.h the offset might be different.

Let me look at the hw.h file directly to see what offsets are defined for these registers.
