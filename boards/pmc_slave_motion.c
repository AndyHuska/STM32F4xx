#include "pmc_slave_motion.h"

#include <math.h>
#include <string.h>

static bool to_integer(double value, int32_t *result)
{
    if(!isfinite(value))
        return false;
    double rounded = round(value);
    if(rounded < INT32_MIN || rounded > INT32_MAX)
        return false;
    *result = (int32_t)rounded;
    return true;
}

pmc_slave_motion_result_t pmc_slave_motion_packet(const pmc_slave_motion_t *motion, pmc_slave_packet_t *packet)
{
    if(!motion || !packet)
        return PmcSlaveMotion_Invalid;
    if(!(motion->axes & PMC_SLAVE_AXES_MASK))
        return PmcSlaveMotion_Local;
    if((motion->axes & ~PMC_SLAVE_AXES_MASK) ||
       (motion->axes & (motion->axes - 1)) ||
       !motion->motion_supported || motion->auxiliary_commands)
        return PmcSlaveMotion_Unsupported;

    int8_t slave_axis = pmc_slave_axis_id(motion->axis);
    if(slave_axis < 0 || motion->axes != (UINT32_C(1) << motion->axis) ||
       !isfinite(motion->steps_per_unit) || motion->steps_per_unit <= 0.0f ||
       !isfinite(motion->max_rate_per_minute) || motion->max_rate_per_minute <= 0.0f ||
       !isfinite(motion->acceleration_per_minute_squared) || motion->acceleration_per_minute_squared <= 0.0f ||
       !isfinite(motion->target) || !isfinite(motion->start))
        return PmcSlaveMotion_Invalid;

    float rate = motion->rapid ? motion->max_rate_per_minute : motion->feed_per_minute;
    if(!isfinite(rate) || rate <= 0.0f)
        return PmcSlaveMotion_Invalid;
    if(rate > motion->max_rate_per_minute)
        rate = motion->max_rate_per_minute;

    pmc_slave_packet_t request = {
        .command = PmcSlave_MoveIndependent | PMC_SLAVE_SYNC_FLAG,
        .word_count = 4,
        .words = {slave_axis}
    };
    int32_t start;
    if(!to_integer((double)motion->target * (double)motion->steps_per_unit, &request.words[1]) ||
       !to_integer((double)motion->start * (double)motion->steps_per_unit, &start))
        return PmcSlaveMotion_Invalid;
    if(start == request.words[1]) {
        *packet = request;
        return PmcSlaveMotion_NoMotion;
    }
    if(
         !to_integer((double)rate * (double)motion->steps_per_unit / 60.0, &request.words[2]) ||
         !to_integer((double)motion->acceleration_per_minute_squared * (double)motion->steps_per_unit / 3600.0, &request.words[3]) ||
       request.words[2] <= 0 || request.words[3] <= 0)
        return PmcSlaveMotion_Invalid;

    *packet = request;
    return PmcSlaveMotion_Remote;
}