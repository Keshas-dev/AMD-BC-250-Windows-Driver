# Mesa build integration for the BC-250 winsys backend.
# Copy src/radv-bc250/ to mesa/src/amd/vulkan/winsys/bc250/
# and add subdir('bc250') to mesa/src/amd/vulkan/winsys/meson.build
# guarded by host_machine.system() == 'windows'.

libradv_bc250 = static_library(
  'radv_bc250',
  files(
    'radv_bc250_winsys.c',
    'radv_bc250_bo.c',
    'radv_bc250_cs.c',
  ),
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux,
    include_directories('../..'),        # src/amd
    include_directories('..'),           # src/amd/vulkan
  ],
  dependencies : [dep_thread, idep_mesautil],
  c_args : [c_vis_args, no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)
