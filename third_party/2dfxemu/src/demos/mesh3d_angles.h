#ifndef MESH3D_ANGLES_H
#define MESH3D_ANGLES_H
#include <stdint.h>
/* Camera/object angles: [xxRRYYPP, roll_low, yaw_low, pitch_low].
 * One turn = 1024 units; signed directions wrap modulo 1024. Highbits bits
 * 7..6 are ignored on input and zero on output. Rotation stays roll/yaw/pitch.
 */
#define TWODFX_3D_ANGLE_MASK 1023u
static inline void twodfx_pack_angles10(uint8_t out[4],
                                      uint16_t roll,uint16_t yaw,uint16_t pitch)
{
    out[0]=(uint8_t)((((roll >> 8)&3u)<<4) |
                     (((yaw >> 8)&3u)<<2) | ((pitch >> 8)&3u));
    out[1]=(uint8_t)roll; out[2]=(uint8_t)yaw; out[3]=(uint8_t)pitch;
}
static inline void twodfx_unpack_angles10(const uint8_t in[4],
                                        uint16_t *roll,uint16_t *yaw,uint16_t *pitch)
{
    *roll=(uint16_t)((((in[0]>>4)&3u)<<8)|in[1]);
    *yaw=(uint16_t)((((in[0]>>2)&3u)<<8)|in[2]);
    *pitch=(uint16_t)(((in[0]&3u)<<8)|in[3]);
}
#endif
