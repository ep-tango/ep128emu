/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
size_t egg_C0_logo_state(uint8_t*,int);
size_t egg_C1_benchmark_state(uint8_t*,int);
size_t egg_C2_primitives_state(uint8_t*,int);
size_t egg_C3_text_state(uint8_t*,int);
size_t egg_C4_blit_state(uint8_t*,int);
size_t egg_C5_pattern_state(uint8_t*,int);
size_t egg_C6_tilemap_state(uint8_t*,int);
size_t egg_C7_pong_basic_state(uint8_t*,int);
size_t egg_C8_pong_wow_state(uint8_t*,int);
size_t egg_C9_wriggler_state(uint8_t*,int);
size_t egg_CA_boulder_state(uint8_t*,int);
size_t egg_CB_arcpie_benchmark_state(uint8_t*,int);
size_t egg_CC_diag_state(uint8_t*,int);
size_t egg_CD_blit_full_state(uint8_t*,int);
size_t egg_CE_layer_state(uint8_t*,int);
size_t egg_CF_tvc_bitpattern_state(uint8_t*,int);
size_t egg_D0_tvc_crisp_tearing_state(uint8_t*,int);
size_t egg_D1_ruler_state(uint8_t*,int);
size_t egg_D2_xy_offset_state(uint8_t*,int);
size_t egg_D3_elite_showcase_state(uint8_t*,int);
size_t egg_D4_graphics_test_state(uint8_t*,int);
size_t egg_D5_public_mesh_state(uint8_t*,int);
static size_t (*const demo_codecs[22])(uint8_t*,int)={egg_C0_logo_state,egg_C1_benchmark_state,egg_C2_primitives_state,egg_C3_text_state,egg_C4_blit_state,egg_C5_pattern_state,egg_C6_tilemap_state,egg_C7_pong_basic_state,egg_C8_pong_wow_state,egg_C9_wriggler_state,egg_CA_boulder_state,egg_CB_arcpie_benchmark_state,egg_CC_diag_state,egg_CD_blit_full_state,egg_CE_layer_state,egg_CF_tvc_bitpattern_state,egg_D0_tvc_crisp_tearing_state,egg_D1_ruler_state,egg_D2_xy_offset_state,egg_D3_elite_showcase_state,egg_D4_graphics_test_state,egg_D5_public_mesh_state};

#define TWODFX_DEMO_STATE_BYTES 131072u
static uint8_t demo_initial[TWODFX_DEMO_STATE_BYTES];
static size_t demo_offsets[23];
static int demo_initialized;
static int demo_states_reset(uint8_t *state){
 if(!demo_initialized){
   size_t offset=0;for(unsigned i=0;i<22;i++){
     size_t n=demo_codecs[i](NULL,0);demo_offsets[i]=offset;
     if(n>TWODFX_DEMO_STATE_BYTES-offset)return 0;
     demo_codecs[i](demo_initial+offset,0);offset+=n;
   }demo_offsets[22]=offset;demo_initialized=1;
 }memcpy(state,demo_initial,TWODFX_DEMO_STATE_BYTES);return 1;
}
static void demo_state_swap(uint8_t *state,unsigned id,int load){
 if(id<22)demo_codecs[id](state+demo_offsets[id],load);
}
