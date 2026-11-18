/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Károly Vaczkó
 */
#include "egg_D5_public_mesh.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "2dfx.h"
#include "twodfx_api.h"
#include "twodfx_egg.h"
#include "egg_D3_elite_showcase.h"
#include "egg_D3_elite_showcase_data.h"

#define D5_SIDES 8u
#define D5_MAX_RINGS 13u /* 4/8/12 forward rings plus one near-side guard */
#define D5_WORLD_SCALE 8 /* one public integer unit = 1/8 scene unit */
#define D5_Q12_PER_UNIT (4096 / D5_WORLD_SCALE)
#define D5_VERTS (D5_SIDES*D5_MAX_RINGS)
#define D5_TRIS (D5_SIDES*(D5_MAX_RINGS-1u)*2u)
#define D5_TUNNEL_BYTES (D5_VERTS*6u+D5_TRIS*4u)
#define D5_TUNNEL0 0x700000u
#define D5_TUNNEL1 0x701000u
#define D5_ASP 0x702000u
#define D5_PLUME 0x703000u

static const int16_t d5_ring_x[8]={164,116,0,-116,-164,-116,0,116};
static const int16_t d5_ring_y[8]={0,72,102,72,0,-72,-102,-72};
static uint8_t d5_buffer[D5_TUNNEL_BYTES];
static uint8_t d5_ship_buffer[19u*6u+64u*4u];
static uint8_t d5_plume_buffer[6u*6u+8u*4u];

static struct {
    bool active,ready,forced,ship,zoom,tvc;
    uint8_t bank, next_bank,ring,mode,profile;
    uint16_t phase;
    uint32_t ticks;
    int32_t travel,ship_z_q12,centre_x_q12,centre_y_q12;
    twodfx_pose3d_t camera, ship_pose;
} d5;
volatile uint32_t g_d5_public_frames=0u, g_d5_public_faults=0u;
volatile uint32_t g_d5_public_mode=0u,g_d5_public_bank=0u;

static void d5_put16(uint8_t *p,int16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)((uint16_t)n>>8);}
static int16_t d5_limit(int32_t n)
{if(n>32767)n=32767;if(n< -32768)n=-32768;return (int16_t)n;}
static void d5_vertex(uint8_t *dst,uint16_t idx,int32_t x,int32_t y,int32_t z)
{
    uint8_t *p=dst+(uint32_t)idx*6u;
    d5_put16(p,d5_limit(x*D5_WORLD_SCALE));
    d5_put16(p+2,d5_limit(y*D5_WORLD_SCALE));
    d5_put16(p+4,d5_limit(z*D5_WORLD_SCALE));
}
static int16_t d5_coord_q12(int32_t q12)
{return d5_limit((q12+(q12>=0?D5_Q12_PER_UNIT/2:-D5_Q12_PER_UNIT/2))/D5_Q12_PER_UNIT);}
static void d5_vertex_q12(uint8_t *dst,uint16_t idx,int32_t x,int32_t y,int32_t z)
{
    uint8_t *p=dst+(uint32_t)idx*6u;
    d5_put16(p,d5_coord_q12(x));d5_put16(p+2,d5_coord_q12(y));d5_put16(p+4,d5_coord_q12(z));
}
static uint8_t d5_ring_count(void)
{return (uint8_t)(5u+(d5.mode%3u)*4u);}
static int32_t d5_spacing(void)
{return (d5.mode%3u==0u)?900:(d5.mode%3u==1u)?450:300;}
static void d5_face(uint8_t *p,uint16_t idx,uint8_t a,uint8_t b,uint8_t c,uint8_t att)
{p+=(uint32_t)idx*4u;p[0]=a;p[1]=b;p[2]=c;p[3]=att;}
/* Continuous world-centre waves controlled by the demo's flight model. */
static int32_t d5_sin(int32_t z,int32_t period,uint16_t phase)
{
    uint16_t p=(uint16_t)((int64_t)z*65536/period+phase);
    uint8_t k=(uint8_t)(p>>8),f=(uint8_t)p;
    return ((int32_t)d3_sine_q12[k]*(256-f)+
            (int32_t)d3_sine_q12[(uint8_t)(k+1u)]*f)/256;
}
/* Keep the original waves, but defer rounding until the public integer
 * coordinate is emitted. In particular, do not difference two independently
 * truncated camera/look-ahead coordinates when calculating yaw and pitch. */
static void d5_world_centre_q12(int32_t z,int32_t *x,int32_t *y)
{
    *x=260*d5_sin(z,10000,0)+85*d5_sin(z,21000,9000);
    *y=125*d5_sin(z,7800,11000)+45*d5_sin(z,15500,38000);
}
static void d5_make_ring(uint8_t ring)
{
    int32_t spacing=d5_spacing();
    int32_t phase=d5.travel%spacing;
    /* The extra ring stays behind the near plane while the old front ring
     * recycles. Retained world segments never disappear at phase wrap. */
    int32_t local_z=180+((int32_t)ring-1)*spacing;
    int32_t z=local_z-phase;
    int32_t cx,cy;
    d5_world_centre_q12(d5.travel+z,&cx,&cy);
    cx-=d5.centre_x_q12;cy-=d5.centre_y_q12;
    /* Gentle tunnel twist. The elliptical profile compensates for
     * display pixel aspect and should not undergo large rotations. */
    int32_t world_z=d5.travel+z;
    int32_t scale_q12=1290*4096+320*d5_sin(world_z,10500,23000);
    int32_t sn=d5_sin(world_z,14500,11000)/11;
    int32_t cs=4096-(sn*sn)/8192;
    for(uint8_t side=0;side<D5_SIDES;side++){
        int32_t x=(int32_t)((int64_t)d5_ring_x[side]*scale_q12/1024);
        int32_t y=(int32_t)((int64_t)d5_ring_y[side]*scale_q12/1024);
        int32_t rx=(int32_t)(((int64_t)x*cs-(int64_t)y*sn)/4096);
        int32_t ry=(int32_t)(((int64_t)x*sn+(int64_t)y*cs)/4096);
        d5_vertex_q12(d5_buffer,(uint16_t)ring*D5_SIDES+side,cx+rx,cy+ry,local_z*4096);
    }
}
static void d5_make_faces(uint8_t rings)
{
    uint8_t *dst=d5_buffer+(uint16_t)rings*D5_SIDES*6u;
    uint16_t t=0;
    for(uint8_t ring=0;ring+1u<rings;ring++){
        for(uint8_t s=0;s<D5_SIDES;s++){
            uint8_t n=(uint8_t)((s+1u)%D5_SIDES);
            uint8_t a=(uint8_t)(ring*D5_SIDES+s);
            uint8_t b=(uint8_t)(ring*D5_SIDES+n);
            uint8_t c=(uint8_t)((ring+1u)*D5_SIDES+n);
            uint8_t e=(uint8_t)((ring+1u)*D5_SIDES+s);
            /* Colour belongs to the world segment, not its recycled slot. */
            uint32_t world_ring=(uint32_t)(d5.travel/d5_spacing()-1+(int32_t)ring);
            uint8_t colour=d5.tvc?((world_ring+s)&1u?9u:13u):((world_ring+s)&1u?5u:7u);
            /* A-B, B-C visible; C-A is the artificial diagonal. */
            d5_face(dst,t++,a,b,c,(uint8_t)(colour|0x30u|0x80u));
            /* A-C hidden, C-E and E-A visible. */
            d5_face(dst,t++,a,c,e,(uint8_t)(colour|0x60u|0x80u));
        }
    }
}
static uint16_t d5_make_asp(void)
{
    memset(d5_ship_buffer,0,sizeof(d5_ship_buffer));
    for(uint8_t i=0;i<19u;i++)
        d5_vertex(d5_ship_buffer,i,d3_asp_vertices[i].x,d3_asp_vertices[i].y,d3_asp_vertices[i].z);
    uint8_t *dst=d5_ship_buffer+19u*6u;uint16_t tris=0;
    for(uint8_t f=0;f<12u;f++){
        const d3_face_t *face=&d3_asp_faces[f];
        uint8_t color=d5.tvc?d3_asp_tvc[f]:d3_asp_ep[f];
        for(uint8_t k=1u;k+1u<face->count;k++){
            uint8_t edges=(uint8_t)(0x20u|(k==1u?0x10u:0u)|
                                    (k+2u==face->count?0x40u:0u));
            /* Two-sided: original Elite faces use per-face normals, not
             * necessarily the general API's winding. This exposes the
             * deliberately simpler public format's occlusion limitations. */
            d5_face(dst,tris++,face->vertex[0],face->vertex[k],face->vertex[k+1u],
                    (uint8_t)(color|edges|0x80u));
        }
    }
    twodfx_upload_raw(D5_ASP,d5_ship_buffer,19u*6u+(uint32_t)tris*4u);
    return tris;
}
static void d5_make_plume(void)
{
    /* A compact rear exhaust: public mesh, deliberately no special effects. */
    static const int16_t p[6][3]={
        {-13,0,0},{13,0,0},{0,-12,0},{0,12,0},{0,0,-54},{0,0,-83}
    };
    for(uint8_t i=0;i<6;i++)d5_vertex(d5_plume_buffer,i,p[i][0],p[i][1],p[i][2]);
    uint8_t *f=d5_plume_buffer+36u;
    for(uint8_t i=0;i<4;i++){
        uint8_t a=(i==0)?0:(i==1)?3:(i==2)?1:2;
        uint8_t b=(i==0)?3:(i==1)?1:(i==2)?2:0;
        d5_face(f,i,a,b,4u,(uint8_t)((d5.tvc?11u:6u)|0x80u));
        d5_face(f,i+4u,a,b,5u,(uint8_t)((d5.tvc?9u:3u)|0x80u));
    }
    twodfx_upload_raw(D5_PLUME,d5_plume_buffer,36u+8u*4u);
    twodfx_3d_define_mesh(2u,D5_PLUME,6u,8u);
}
static void d5_publish(void)
{
    uint8_t rings=d5_ring_count();
    uint16_t vertices=(uint16_t)rings*D5_SIDES;
    uint16_t faces=(uint16_t)(rings-1u)*D5_SIDES*2u;
    uint32_t base=(d5.next_bank==0u)?D5_TUNNEL0:D5_TUNNEL1;
    /* Single upload into the INACTIVE resource bank. The front bank remains
     * untouched until the renderer has finished using it. */
    twodfx_upload_raw(base,d5_buffer,vertices*6u+(uint32_t)faces*4u);
    twodfx_3d_define_mesh(0u,base,vertices,faces);
    uint16_t far=(rings==5u)?920u:(rings==9u)?2070u:2490u;
    /* Public world units are arbitrary. Scale all geometry, translations and
     * clip distances equally: same view/motion, finer integer placement,
     * unchanged 10-bit angles and no hidden renderer/camera precision. */
    twodfx_3d_set_projection(365u,300u,96u*D5_WORLD_SCALE,far*D5_WORLD_SCALE);
    twodfx_3d_set_camera(d5.camera);
    twodfx_reset_2dpt_background();
    twodfx_2d_set_clip_rect(TWODFX_LAYER_BACKGROUND,0,
        TWODFX_BASIC_SAFE_MIN_X,TWODFX_BASIC_SAFE_MIN_Y,
        TWODFX_BASIC_SAFE_MAX_X-TWODFX_BASIC_SAFE_MIN_X+1,
        TWODFX_BASIC_SAFE_MAX_Y-TWODFX_BASIC_SAFE_MIN_Y+1);
    twodfx_2d_set_color(TWODFX_LAYER_BACKGROUND,1,d5.tvc?15u:4u,0u);
    twodfx_mesh_mode_t mode=(d5.mode<3u)?TWODFX_MESH_WIREFRAME:TWODFX_MESH_FILLED;
    twodfx_3d_draw_mesh(0u,2u,0u,(twodfx_pose3d_t){0},mode);
    twodfx_2d_ret(TWODFX_LAYER_BACKGROUND,3u);

    twodfx_reset_2dpt_foreground();
    twodfx_2d_set_clip_rect(TWODFX_LAYER_FOREGROUND,0,
        TWODFX_BASIC_SAFE_MIN_X,TWODFX_BASIC_SAFE_MIN_Y,
        TWODFX_BASIC_SAFE_MAX_X-TWODFX_BASIC_SAFE_MIN_X+1,
        TWODFX_BASIC_SAFE_MAX_Y-TWODFX_BASIC_SAFE_MIN_Y+1);
    uint8_t slot=1u;
    if(d5.ship){
        /* Exhaust is positioned in ship local Z by its own mesh vertices. */
        twodfx_3d_draw_mesh(1u,slot++,2u,d5.ship_pose,TWODFX_MESH_FILLED);
        twodfx_2d_set_color(TWODFX_LAYER_FOREGROUND,slot++,d5.tvc?15u:4u,0u);
        twodfx_3d_draw_mesh(1u,slot++,1u,d5.ship_pose,TWODFX_MESH_FWE);
    }
    /* Six phase/mode pips; all game logic lives in the egg. */
    twodfx_2d_set_color(TWODFX_LAYER_FOREGROUND,slot++,d5.tvc?15u:4u,0u);
    for(uint8_t k=0u;k<6u;k++){
        uint16_t x=TWODFX_BASIC_SAFE_MIN_X+8u+(uint16_t)k*16u;
        twodfx_2d_fill_rect(TWODFX_LAYER_FOREGROUND,slot++,x,
                  TWODFX_BASIC_SAFE_MIN_Y+8u,k==d5.mode?12u:5u,k==d5.mode?6u:3u);
    }
    twodfx_2d_ret(TWODFX_LAYER_FOREGROUND,slot);
    d5.bank=d5.next_bank;
    g_d5_public_bank=d5.bank;
    g_d5_public_mode=d5.mode;
    ++g_d5_public_frames;
    d5.ready=true;
}
/* Round signed Q8 fixed-point estimates to full public 10-bit angle units.
 * D5 never applies a screen-space compensation or hidden camera precision. */
static int32_t d5_round_angle_q8(int32_t q8)
{return (q8>=0 ? q8+128 : q8-128)/256;}
static void d5_prepare(void)
{
    uint16_t step=(d5.profile==0u)?15u:(d5.profile==1u)?25u:34u;
    d5.travel+=step;
    int32_t cx,cy,lookx,looky,sx,sy;
    d5_world_centre_q12(d5.travel,&cx,&cy);
    d5.centre_x_q12=cx;d5.centre_y_q12=cy;
    d5_world_centre_q12(d5.travel+550,&lookx,&looky);
    /* 1024/(2*pi) ~= 162.97 angle units per radian.
     * Both camera and ship use the same public 10-bit angle scale. */
    int32_t yaw_q8=(int32_t)((int64_t)(lookx-cx)*163*256/(550*4096));
    int32_t pitch_q8=(int32_t)(-(int64_t)(looky-cy)*163*256/(550*4096));
    int32_t yaw=d5_round_angle_q8(yaw_q8);
    int32_t pitch=d5_round_angle_q8(pitch_q8);
    int32_t spacing=d5_spacing();
    d5.camera=(twodfx_pose3d_t){
        d5_coord_q12(48*d5_sin((int32_t)d5.ticks,390,9000)*d5.profile/2),
        d5_coord_q12(30*d5_sin((int32_t)d5.ticks,510,26000)*d5.profile/2),
        (int16_t)((d5.travel%spacing)*D5_WORLD_SCALE),0,(uint16_t)yaw,(uint16_t)pitch};
    if(d5.zoom)d5.ship_z_q12=445*4096+80*d5_sin((int32_t)d5.ticks,160,0);
    else d5.ship_z_q12=410*4096;
    d5_world_centre_q12(d5.travel+d5.ship_z_q12/4096,&sx,&sy);
    d5.ship_pose=(twodfx_pose3d_t){d5_coord_q12(sx-cx+d5.camera.x*D5_Q12_PER_UNIT+32*d5_sin((int32_t)d5.ticks,115,0)),
                       d5_coord_q12(sy-cy+25*4096+d5.camera.y*D5_Q12_PER_UNIT),
                       (int16_t)(d5_coord_q12(d5.ship_z_q12)+d5.camera.z),
                       (uint16_t)(d5_sin((int32_t)d5.ticks,200,0)/88),
                       (uint16_t)(yaw/2),0};
    d5.next_bank=(uint8_t)(d5.bank^1u);
    d5.ring=0u;d5.ready=false;
}
void egg_D5_public_mesh_start(void)
{
    memset(&d5,0,sizeof(d5));
    d5.tvc=twodfx_get_machine()==TWODFX_MACHINE_TVC;
    d5.active=true;d5.mode=2u;d5.profile=2u;d5.forced=true;d5.ship=true;d5.zoom=true;
    g_d5_public_frames=g_d5_public_faults=0u;
    twodfx_set_engine_mode(TWODFX_ENGINE_FORCED_ON);
    twodfx_set_fps(TWODFX_FPS_50);
    twodfx_reset_sprites();
    twodfx_3d_define_mesh(1u,D5_ASP,19u,d5_make_asp());
    d5_make_plume();
    d5_prepare();
    for(uint8_t r=0;r<D5_MAX_RINGS;r++)d5_make_ring(r);
    d5_make_faces(d5_ring_count());
    d5_publish();
}
void egg_D5_public_mesh_stop(void){d5.active=false;d5.ready=true;}
bool egg_D5_public_mesh_ready(void){return !d5.active || d5.ready;}
void egg_D5_public_mesh_tick(void)
{
    if(!d5.active)return;
    uint8_t v;
    while(twodfx_egg_read_f9(&v)){
        if(v<6u){d5.mode=v;d5.forced=true;d5.phase=0u;}
        else if(v==255u){d5.forced=false;d5.phase=0u;}
        else if(v>=240u&&v<=242u)d5.profile=v-240u;
        else if(v==243u)d5.ship=true;
        else if(v==244u)d5.ship=false;
        else if(v==245u)d5.zoom=false;
        else if(v==246u)d5.zoom=true;
    }
    if(!d5.forced && ++d5.phase>=400u){d5.phase=0u;d5.mode=(uint8_t)((d5.mode+1u)%6u);}
    ++d5.ticks;
    d5_prepare();
}
void egg_D5_public_mesh_step(void)
{
    if(!d5.active||d5.ready)return;
    if(d5.ring<d5_ring_count()){d5_make_ring(d5.ring++);return;}
    if(d5.ring==d5_ring_count()){
        uint8_t rings=d5_ring_count();
        d5_make_faces(rings);d5.ring++;
        return;
    }
    d5_publish();
}

/* Portable per-device state codec. */
#include "demo_state_codec.h"
size_t egg_D5_public_mesh_state(uint8_t *data,int load){
 size_t offset=0;
  for(size_t i0=0;i0<sizeof(d5_buffer)/sizeof((d5_buffer)[0]);++i0){
  STATE_U(d5_buffer[i0],8);
  }
  for(size_t i0=0;i0<sizeof(d5_ship_buffer)/sizeof((d5_ship_buffer)[0]);++i0){
  STATE_U(d5_ship_buffer[i0],8);
  }
  for(size_t i0=0;i0<sizeof(d5_plume_buffer)/sizeof((d5_plume_buffer)[0]);++i0){
  STATE_U(d5_plume_buffer[i0],8);
  }
  STATE_U(d5.active,8);
  STATE_U(d5.ready,8);
  STATE_U(d5.forced,8);
  STATE_U(d5.ship,8);
  STATE_U(d5.zoom,8);
  STATE_U(d5.tvc,8);
  STATE_U(d5.bank,8);
  STATE_U(d5.next_bank,8);
  STATE_U(d5.ring,8);
  STATE_U(d5.mode,8);
  STATE_U(d5.profile,8);
  STATE_U(d5.phase,16);
  STATE_U(d5.ticks,32);
  STATE_S(d5.travel,32);
  STATE_S(d5.ship_z_q12,32);
  STATE_S(d5.centre_x_q12,32);
  STATE_S(d5.centre_y_q12,32);
  STATE_S(d5.camera.x,16);
  STATE_S(d5.camera.y,16);
  STATE_S(d5.camera.z,16);
  STATE_U(d5.camera.roll,16);
  STATE_U(d5.camera.yaw,16);
  STATE_U(d5.camera.pitch,16);
  STATE_S(d5.ship_pose.x,16);
  STATE_S(d5.ship_pose.y,16);
  STATE_S(d5.ship_pose.z,16);
  STATE_U(d5.ship_pose.roll,16);
  STATE_U(d5.ship_pose.yaw,16);
  STATE_U(d5.ship_pose.pitch,16);
 return offset;
}
