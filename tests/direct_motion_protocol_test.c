#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../plugins/direct_motion.h"

static void put(uint8_t *bytes, uint32_t value)
{
    for(unsigned index = 0; index < 4; index++)
        bytes[index] = (uint8_t)(value >> (index * 8));
}

int main(void)
{
    uint8_t bytes[33] = {0};
    direct_motion_velocity_frame_t frame;
    bytes[0] = 7;
    put(bytes + 1, 65536);
    put(bytes + 5, (uint32_t)-65536);
    put(bytes + 9, 0);
    assert(direct_motion_decode_velocity(8, bytes, 13, &frame));
    assert(frame.mask == 7 && frame.velocity_q16[0] == 65536 && frame.velocity_q16[1] == -65536);
    for(unsigned axis = 3; axis < 8; axis++)
        assert(frame.velocity_q16[axis] == 0);
    bytes[0] = 8;
    assert(!direct_motion_decode_velocity(8, bytes, 13, &frame));
    bytes[0] = 0xFF;
    for(unsigned axis = 0; axis < 8; axis++)
        put(bytes + 1 + axis * 4, axis & 1 ? (uint32_t)INT32_MIN : INT32_MAX);
    assert(direct_motion_decode_velocity(8, bytes, 33, &frame));
    for(unsigned axis = 0; axis < 8; axis++)
        assert(frame.velocity_q16[axis] == (axis & 1 ? INT32_MIN : INT32_MAX));
    assert(!direct_motion_decode_velocity(3, bytes, 33, &frame));
    assert(!direct_motion_decode_velocity(8, bytes, 32, &frame));
    assert(!direct_motion_decode_velocity(8, bytes, 34, &frame));
    bytes[0] = 0x20;
    assert(direct_motion_decode_velocity(6, bytes, 25, &frame));
    bytes[0] = 0x40;
    assert(!direct_motion_decode_velocity(6, bytes, 25, &frame));
    assert(!direct_motion_decode_velocity(0, bytes, 13, &frame));
    assert(!direct_motion_decode_velocity(9, bytes, 13, &frame));
    assert(!direct_motion_decode_velocity(8, NULL, 13, &frame));
    puts("DirectMotion velocity protocol tests passed");
    return 0;
}