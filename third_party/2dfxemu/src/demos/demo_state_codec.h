/* SPDX-License-Identifier: MIT */
#ifndef TWODFX_DEMO_STATE_CODEC_H
#define TWODFX_DEMO_STATE_CODEC_H
#include <stddef.h>
#include <stdint.h>
/* Canonical little-endian scalars; no structure padding, pointers or enum ABI. */
static inline uint64_t demo_scalar(uint8_t *data,size_t *offset,int load,uint64_t value,unsigned bits){
 uint64_t result=0;for(unsigned b=0;b<bits/8;b++){
   if(data){if(load)result|=(uint64_t)data[*offset]<<(8*b);else data[*offset]=(uint8_t)(value>>(8*b));}++*offset;
 }return load&&data?result:value;
}
#define STATE_U(v,bits) do{uint64_t n=demo_scalar(data,&offset,load,(uint64_t)(v),bits);if(load&&data)(v)=n;}while(0)
#define STATE_S(v,bits) do{uint64_t n=demo_scalar(data,&offset,load,(uint64_t)(v),bits);if(load&&data)(v)=(n>>(bits-1))?-(int64_t)((UINT64_C(1)<<(bits-1))-(n&((UINT64_C(1)<<(bits-1))-1))):(int64_t)n;}while(0)
#endif
