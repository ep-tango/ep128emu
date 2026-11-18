/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Károly Vaczkó
 */

#include "twodfx_3d.h"
#include <stdbool.h>
#include "../demos/mesh3d_angles.h"
#include "../demos/egg_D3_elite_showcase.h"
#include <stdlib.h>
#include <string.h>

static uint16_t u16(const uint8_t *p) {
  return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
}
static int16_t i16(const uint8_t *p) {
  uint16_t v=u16(p);
  return (int16_t)(v<=32767u ? (int)v : (int)v-65536);
}
void twodfx_3d_unpack_angles(const uint8_t p[4],
                            uint16_t *roll,uint16_t *yaw,uint16_t *pitch) {
  twodfx_unpack_angles10(p,roll,yaw,pitch);
}
void twodfx_3d_reset(twodfx_3d_scene *s) {
  if(!s)return;
  memset(s,0,sizeof *s);
  s->projection_shadow.fx=579u;s->projection_shadow.fy=300u;
  s->projection_shadow.near_z=96u;s->projection_shadow.far_z=2490u;
  s->projection_active=s->projection_shadow;
}
void twodfx_3d_latch(twodfx_3d_scene *s) {
  if(!s)return;
  memcpy(s->mesh_active,s->mesh_shadow,sizeof s->mesh_active);
  s->camera_active=s->camera_shadow;
  s->projection_active=s->projection_shadow;
}
void twodfx_3d_define(twodfx_3d_scene *s,unsigned id,uint32_t address,
                       unsigned vertices,unsigned faces) {
  uint64_t bytes=(uint64_t)vertices*6u+(uint64_t)faces*4u;
  if(!s||id>=256u)return;
  s->mesh_shadow[id].address=address;
  s->mesh_shadow[id].vertices=(uint16_t)vertices;
  s->mesh_shadow[id].faces=(uint16_t)(vertices>0u && vertices<=256u &&
    faces>0u && faces<=512u && address<8388608u && bytes<=8388608u-address?faces:0u);
}
void twodfx_3d_scene_set_projection(twodfx_3d_scene *s,uint16_t fx,
                              uint16_t fy,uint16_t near_z,uint16_t far_z) {
  if(!s||!fx||!fy||!near_z||far_z<=near_z)return;
  s->projection_shadow.fx=fx;s->projection_shadow.fy=fy;
  s->projection_shadow.near_z=near_z;s->projection_shadow.far_z=far_z;
}
void twodfx_3d_scene_set_camera(twodfx_3d_scene *s,const uint8_t p[10]) {
  if(!s||!p)return;
  s->camera_shadow.x=i16(p);
  s->camera_shadow.y=i16(p+2);
  s->camera_shadow.z=i16(p+4);
  twodfx_3d_unpack_angles(p+6,&s->camera_shadow.roll,
    &s->camera_shadow.yaw,&s->camera_shadow.pitch);
}

/* Camera-space coordinates use 1/256 world units. Each Q12 product is
 * truncated separately, including negative products, before addition. */
typedef struct {int32_t x,y,z;} vector3;
typedef struct {int x,y;} point2;
static int sine10(unsigned a){
 unsigned i=(a>>2)&255,f=a&3;
 return d3_sine_q12[i]+((d3_sine_q12[(i+1)&255]-d3_sine_q12[i])*(int)f)/4;
}
static int32_t product(int32_t v,int factor){return (int32_t)((int64_t)v*factor/4096);}
static vector3 rotate_axis(vector3 v,unsigned axis,unsigned angle,int inverse){
 int s=sine10(angle),c=sine10(angle+256),a,b;
 if(inverse)s=-s;
 if(axis==2){a=product(v.x,c)-product(v.y,s);b=product(v.x,s)+product(v.y,c);v.x=a;v.y=b;}
 else if(axis==1){a=product(v.x,c)+product(v.z,s);b=-product(v.x,s)+product(v.z,c);v.x=a;v.z=b;}
 else{a=product(v.y,c)-product(v.z,s);b=product(v.y,s)+product(v.z,c);v.y=a;v.z=b;}
 return v;
}
static vector3 cut_depth(vector3 a,vector3 b,int z){
 int64_t den=(int64_t)b.z-a.z;
 if(!den)return a;
 vector3 q={a.x+(int32_t)(((int64_t)z-a.z)*((int64_t)b.x-a.x)/den),
            a.y+(int32_t)(((int64_t)z-a.z)*((int64_t)b.y-a.y)/den),z};return q;
}
static unsigned clip_depth(vector3 *out,const vector3 *in,unsigned count,int z,int upper){
 unsigned n=0;if(!count)return 0;
 for(unsigned i=0;i<count;i++){
  vector3 a=in[(i+count-1)%count],b=in[i];
  int ai=upper?a.z<=z:a.z>=z,bi=upper?b.z<=z:b.z>=z;
  if(ai!=bi)out[n++]=cut_depth(a,b,z);
  if(bi)out[n++]=b;
 }return n;
}
static point2 project_point(vector3 v,const twodfx_3d_projection*p,int cx,int cy){
 int64_t dx=(int64_t)v.x*p->fx/v.z,dy=(int64_t)v.y*p->fy/v.z;
 if(dx>1000000)dx=1000000;
 if(dx< -1000000)dx=-1000000;
 if(dy>1000000)dy=1000000;
 if(dy< -1000000)dy=-1000000;
 point2 q={cx+(int)dx,cy-(int)dy};return q;
}
static unsigned screen_region(point2 p,const twodfx_raster*r){
 return (p.x<r->x0?1:0)|(p.x>r->x1?2:0)|(p.y<r->y0?4:0)|(p.y>r->y1?8:0);
}
static void mesh_edge(twodfx_raster*r,vector3 a,vector3 b,const twodfx_3d_projection*p,int cx,int cy,uint8_t c){
 int near=p->near_z*256,far=p->far_z*256;
 if((a.z<near&&b.z<near)||(a.z>far&&b.z>far)||r->x0>r->x1||r->y0>r->y1)return;
 if(a.z<near)a=cut_depth(a,b,near);else if(b.z<near)b=cut_depth(a,b,near);
 if(a.z>far)a=cut_depth(a,b,far);else if(b.z>far)b=cut_depth(a,b,far);
 point2 u=project_point(a,p,cx,cy),v=project_point(b,p,cx,cy);
 for(unsigned n=0;n<12;n++){
  unsigned first=screen_region(u,r),last=screen_region(v,r),mask=first?first:last;
  if(!(first|last)){twodfx_raster_line(r,u.x,u.y,v.x,v.y,c);return;}
  if(first&last)return;
  point2 q;
  if(mask&12){
   if(u.y==v.y)return;
   q.y=(mask&4)?r->y0:r->y1;
   q.x=u.x+(int)((int64_t)(v.x-u.x)*(q.y-u.y)/(v.y-u.y));
  }else{
   if(u.x==v.x)return;
   q.x=(mask&2)?r->x1:r->x0;
   q.y=u.y+(int)((int64_t)(v.y-u.y)*(q.x-u.x)/(v.x-u.x));
  }
  if(first)u=q;else v=q;
 }
}
static int face_front(vector3 a,vector3 b,vector3 c){
 int64_t u[3]={(b.x-a.x)/64,(b.y-a.y)/64,(b.z-a.z)/64};
 int64_t v[3]={(c.x-a.x)/64,(c.y-a.y)/64,(c.z-a.z)/64};
 return (u[1]*v[2]-u[2]*v[1])*(a.x/64)+(u[2]*v[0]-u[0]*v[2])*(a.y/64)+(u[0]*v[1]-u[1]*v[0])*(a.z/64)<0;
}
void twodfx_3d_render(const twodfx_3d_scene*s,const uint8_t*ram,twodfx_raster*r,
 const uint8_t entry[11],unsigned mode,uint8_t edge_colour,int ox,int oy,
 int origin_x,int origin_y,int optical_x,int optical_y){
 if(!s||!ram||!r||!entry||mode>2)return;
 const twodfx_3d_mesh*m=&s->mesh_active[entry[10]];
 if(!m->vertices||!m->faces||m->vertices>256||m->faces>512||m->address>=8388608u||
    (uint64_t)m->address+m->vertices*6u+m->faces*4u>8388608u)return;
 vector3 vertices[256];struct{int32_t depth;unsigned index;}order[512];unsigned count=0;
 uint16_t angles[3];twodfx_3d_unpack_angles(entry+6,&angles[2],&angles[1],&angles[0]);
 const twodfx_3d_pose*camera=&s->camera_active;
 unsigned inverse[3]={camera->pitch,camera->yaw,camera->roll};
 const twodfx_3d_projection*p=&s->projection_active;
 int cx=optical_x+ox-origin_x,cy=optical_y+oy-origin_y,near=p->near_z*256,far=p->far_z*256;
 for(unsigned i=0;i<m->vertices;i++){
  const uint8_t*b=ram+m->address+6*i;
  vector3 v={i16(b)*256,i16(b+2)*256,i16(b+4)*256};
  for(int axis=2;axis>=0;axis--)v=rotate_axis(v,axis,angles[axis],0);
  v.x+=(i16(entry)-camera->x)*256;v.y+=(i16(entry+2)-camera->y)*256;v.z+=(i16(entry+4)-camera->z)*256;
  for(unsigned axis=0;axis<3;axis++)v=rotate_axis(v,axis,inverse[axis],1);
  vertices[i]=v;
 }
 const uint8_t*faces=ram+m->address+6*m->vertices;
 for(unsigned i=0;i<m->faces;i++){
  const uint8_t*f=faces+4*i;if(f[0]>=m->vertices||f[1]>=m->vertices||f[2]>=m->vertices)continue;
  vector3 a=vertices[f[0]],b=vertices[f[1]],c=vertices[f[2]];
  if(!(f[3]&128)&&!face_front(a,b,c))continue;
  if((a.z<near&&b.z<near&&c.z<near)||(a.z>far&&b.z>far&&c.z>far))continue;
  int32_t depth=(int32_t)(((int64_t)a.z+b.z+c.z)/3);unsigned j=count++;
  while(j&&depth>order[j-1].depth){order[j]=order[j-1];j--;}
  order[j].depth=depth;order[j].index=i;
 }
 for(unsigned i=0;i<count;i++){
  const uint8_t*f=faces+4*order[i].index;
  vector3 tri[3]={vertices[f[0]],vertices[f[1]],vertices[f[2]]};
  if(mode){
   vector3 scratch[8],poly[8];unsigned n=clip_depth(scratch,tri,3,near,0);n=clip_depth(poly,scratch,n,far,1);
   if(n>=3){point2 first=project_point(poly[0],p,cx,cy);
    for(unsigned j=2;j<n;j++){
     point2 a=project_point(poly[j-1],p,cx,cy),b=project_point(poly[j],p,cx,cy);
     twodfx_raster_fill_triangle(r,first.x,first.y,a.x,a.y,b.x,b.y,f[3]&15);
    }
   }
  }
  if(mode!=1)for(unsigned edge=0;edge<3;edge++)if(f[3]&(16u<<edge))mesh_edge(r,tri[edge],tri[(edge+1)%3],p,cx,cy,edge_colour);
 }
}
