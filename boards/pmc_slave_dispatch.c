#include "driver.h"

#if defined(BOARD_PMC_02_AA)

#include "pmc_slave_dispatch.h"
#include "pmc_slave_motion.h"
#include "grbl/motion_control.h"
#include "grbl/protocol.h"
#include "grbl/state_machine.h"
#include "grbl/task.h"
#include <math.h>
#include "plugins/direct_motion.h"

static pmc_slave_bridge_t *remote;
static on_motion_dispatch_ptr previous_dispatch;
static pmc_slave_event_handler_t previous_event;
static void *previous_event_context;
static on_state_change_ptr previous_state;
static on_reset_ptr previous_reset;
static on_jog_cancel_ptr previous_jog_cancel;
static home_machine_ptr previous_home;
static volatile bool stop_requested;
static bool waiting, completed, succeeded;
static uint32_t waiting_sequence;
static uint32_t last_stop_flags;
static uint32_t now_ms(void);

#if VELOCITY_JOG_ENABLE
typedef struct {
    uint16_t sequence;
    uint8_t type;
    bool relative;
    int32_t target, velocity, acceleration;
} remote_dm_command_t;

typedef struct {
    volatile bool pending;
    volatile uint8_t mode;
    remote_dm_command_t command;
    remote_dm_command_t active_command;
    uint32_t rpc_sequence;
} remote_dm_axis_t;

static remote_dm_axis_t dm_remote[2];

static void dm_failed(uint8_t index)
{
    remote_dm_axis_t *axis = &dm_remote[index];
    direct_motion_report_remote_error(axis->active_command.sequence, axis->active_command.type, index + 6);
    axis->mode = 0;
    axis->rpc_sequence = 0;
    axis->pending = false;
}

static bool dm_remote_move(uint8_t axis, bool relative, int32_t target, int32_t velocity,
                           int32_t acceleration, uint16_t sequence)
{
    if(!remote || !remote->online || waiting || axis < 6 || axis > 7 ||
       !remote->status_valid || now_ms() - remote->status_time > 2000)
        return false;
    remote_dm_axis_t *job = &dm_remote[axis - 6];
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    bool available = !job->mode && !job->pending && !job->rpc_sequence;
    if(available) {
        job->command = (remote_dm_command_t){.sequence = sequence, .type = relative ? 0x12 : 0x11,
            .relative = relative, .target = target, .velocity = velocity, .acceleration = acceleration};
        job->mode = 2;
        job->pending = true;
    }
    __set_PRIMASK(irq);
    return available;
}

static bool dm_remote_velocity(uint8_t axis, int32_t velocity, uint16_t sequence)
{
    if(!remote || !remote->online || waiting || axis < 6 || axis > 7)
        return false;
    remote_dm_axis_t *job = &dm_remote[axis - 6];
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    bool available = job->mode != 2;
    if(available) {
        job->command = (remote_dm_command_t){.sequence = sequence, .type = 0x10, .velocity = velocity};
        job->mode = 1;
        job->pending = true;
    }
    __set_PRIMASK(irq);
    return available;
}

static bool dm_remote_stop(uint8_t mask, bool immediate)
{
    (void)immediate;
    if(mask & PMC_SLAVE_AXES_MASK)
        stop_requested = true;
    return true;
}

static bool dm_remote_state(uint8_t axis, direct_motion_remote_state_t *snapshot)
{
    if(axis < 6 || axis > 7 || !snapshot)
        return false;
    snapshot->mode = remote->online ? dm_remote[axis - 6].mode : 0;
    snapshot->rate_per_minute = 0.0f;
    if(remote->online && remote->status_valid && now_ms() - remote->status_time <= 2000 &&
       settings.axis[axis].steps_per_mm > 0.0f)
        snapshot->rate_per_minute = (float)remote->status.velocity[axis - 6] * 60.0f / settings.axis[axis].steps_per_mm;
    if(!snapshot->mode && snapshot->rate_per_minute != 0.0f)
        snapshot->mode = 1;
    return true;
}

static void dm_poll(void)
{
    for(uint8_t index = 0; index < 2; index++) {
        remote_dm_axis_t *job = &dm_remote[index];
        if(!job->pending || job->rpc_sequence)
            continue;
        uint32_t irq = __get_PRIMASK();
        __disable_irq();
        remote_dm_command_t command = job->command;
        job->pending = false;
        job->active_command = command;
        __set_PRIMASK(irq);
        uint8_t axis = index + 6;
        pmc_slave_packet_t packet;
        bool submitted = false;
        if(command.type == 0x10) {
            double rate = (double)command.velocity * (double)settings.axis[axis].steps_per_mm / 65536.0;
            if(isfinite(rate) && rate >= INT32_MIN && rate <= INT32_MAX) {
                packet = (pmc_slave_packet_t){.command = PmcSlave_JogVelocity, .word_count = 2,
                    .words = {pmc_slave_axis_id(axis), (int32_t)round(rate)}};
                submitted = pmc_slave_bridge_submit(remote, &packet, now_ms(), 2000, &job->rpc_sequence);
            }
        } else {
            float start = (float)sys.position[axis] / settings.axis[axis].steps_per_mm;
            pmc_slave_motion_t motion = {
                .steps_per_unit = settings.axis[axis].steps_per_mm,
                .max_rate_per_minute = settings.axis[axis].max_rate,
                .acceleration_per_minute_squared = command.acceleration > 0 ?
                    (float)command.acceleration * (3600.0f / 65536.0f) : settings.axis[axis].acceleration,
                .target = (float)command.target / 65536.0f + (command.relative ? start : 0.0f),
                .start = start,
                .feed_per_minute = command.velocity > 0 ? (float)command.velocity * (60.0f / 65536.0f) : settings.axis[axis].max_rate,
                .axes = UINT32_C(1) << axis, .axis = axis, .motion_supported = true
            };
            pmc_slave_motion_result_t result = pmc_slave_motion_packet(&motion, &packet);
            if(result == PmcSlaveMotion_NoMotion) {
                job->mode = 0;
                continue;
            }
            if(result == PmcSlaveMotion_Remote && grbl.check_travel_limits) {
                float target[N_AXIS];
                system_convert_array_steps_to_mpos(target, sys.position);
                target[axis] = motion.target;
                if(grbl.check_travel_limits(target, (axes_signals_t){.mask = bit(axis)}, true, &sys.work_envelope)) {
                    double timeout = fabs((double)motion.target - (double)start) * 180000.0 /
                        (double)motion.feed_per_minute + 10000.0;
                    if(timeout <= INT32_MAX)
                        submitted = pmc_slave_bridge_submit_independent(remote, &packet, now_ms(), (uint32_t)timeout, &job->rpc_sequence);
                }
            }
        }
        if(!submitted)
            dm_failed(index);
    }
}
#endif

static uint32_t now_ms(void)
{
    return hal.get_elapsed_ticks();
}

static void on_event(void *context, pmc_slave_event_t event, uint32_t sequence,
                     const pmc_slave_packet_t *reply)
{
    (void)context;
#if VELOCITY_JOG_ENABLE
    for(uint8_t index = 0; index < 2; index++) {
        remote_dm_axis_t *job = &dm_remote[index];
        if(!job->rpc_sequence || job->rpc_sequence != sequence)
            continue;
        if(event == PmcSlaveEvent_Timeout || event == PmcSlaveEvent_LinkLost)
            dm_failed(index);
        else if(event == PmcSlaveEvent_Reply && reply) {
            pmc_slave_reply_kind_t kind = pmc_slave_reply_kind(reply);
            if(kind == PmcSlaveReply_Error || (reply->word_count == 1 && reply->words[0] == 0))
                dm_failed(index);
            else if(job->mode == 1) {
                job->rpc_sequence = 0;
                if(job->active_command.velocity == 0 && !job->pending)
                    job->mode = 0;
            } else if(kind == PmcSlaveReply_Completion) {
                job->rpc_sequence = 0;
                job->mode = 0;
            }
        }
    }
#endif
#if N_AXIS >= 8
    if(event == PmcSlaveEvent_Status && reply) {
        hal.irq_disable();
        sys.position[6] = reply->words[1];
        sys.position[7] = reply->words[2];
        hal.irq_enable();
    }
#endif
    if(waiting && sequence == waiting_sequence) {
        if(event == PmcSlaveEvent_Timeout || event == PmcSlaveEvent_LinkLost) {
            completed = true;
            succeeded = false;
        } else if(event == PmcSlaveEvent_Reply && reply) {
            pmc_slave_reply_kind_t kind = pmc_slave_reply_kind(reply);
            if(kind == PmcSlaveReply_Completion || kind == PmcSlaveReply_Error ||
               (reply->word_count == 1 && reply->words[0] == 0)) {
                completed = true;
                succeeded = kind == PmcSlaveReply_Completion && reply->words[0] == 1;
            }
        }
    }
    if(previous_event)
        previous_event(previous_event_context, event, sequence, reply);
}

static void poll_remote(void *data)
{
    (void)data;
    uint32_t flags = sys.rt_exec_state & (EXEC_FEED_HOLD | EXEC_STOP | EXEC_RESET | EXEC_MOTION_CANCEL);
    if(flags && flags != last_stop_flags)
        stop_requested = true;
    last_stop_flags = flags;
    if(stop_requested) {
        stop_requested = false;
        pmc_slave_bridge_disconnect(remote);
        pmc_slave_packet_t stop = {.command = PmcSlave_Stop, .word_count = 1};
        pmc_slave_bridge_submit(remote, &stop, now_ms(), 2000, NULL);
#if VELOCITY_JOG_ENABLE
        for(uint8_t index = 0; index < 2; index++) {
            dm_remote[index].pending = false;
            dm_remote[index].mode = 0;
        }
#endif
    }
#if VELOCITY_JOG_ENABLE
    dm_poll();
#endif
    pmc_slave_bridge_poll(remote, now_ms());
}

static void on_state_change(sys_state_t state)
{
    if(state & (STATE_HOLD | STATE_ALARM | STATE_ESTOP))
        stop_requested = true;
    if(previous_state)
        previous_state(state);
}

static void on_reset(void)
{
    stop_requested = true;
    if(previous_reset)
        previous_reset();
}

static void on_jog_cancel(sys_state_t state)
{
    stop_requested = true;
    if(previous_jog_cancel)
        previous_jog_cancel(state);
}

static bool home_local(axes_signals_t cycle, axes_signals_t auto_square)
{
    if(!cycle.mask || (cycle.mask & PMC_SLAVE_AXES_MASK))
        return false;
    return previous_home && previous_home(cycle, auto_square);
}

static status_code_t dispatch_motion(const motion_dispatch_request_t *request,
                                     plan_line_data_t *data, bool *handled)
{
    *handled = false;
    uint32_t axes = request->axes.mask;
    if(request->raw) {
        if(axes & PMC_SLAVE_AXES_MASK)
            return Status_GcodeUnsupportedCommand;
        return previous_dispatch ? previous_dispatch(request, data, handled) : Status_OK;
    }
    if(!(axes & PMC_SLAVE_AXES_MASK)) {
        if(request->block->non_modal_command == NonModal_GoHome_0 ||
           request->block->non_modal_command == NonModal_GoHome_1)
            return Status_GcodeUnsupportedCommand;
        return previous_dispatch ? previous_dispatch(request, data, handled) : Status_OK;
    }

    uint8_t axis = (axes & UINT32_C(0x40)) ? 6 : 7;
    const parser_block_t *block = request->block;
    pmc_slave_motion_t motion = {
        .steps_per_unit = settings.axis[axis].steps_per_mm,
        .max_rate_per_minute = settings.axis[axis].max_rate,
        .acceleration_per_minute_squared = settings.axis[axis].acceleration,
        .target = block->values.xyz[axis],
        .start = request->position[axis],
        .feed_per_minute = block->values.f,
        .axes = axes, .axis = axis,
        .rapid = block->modal.motion == MotionMode_Seek,
        .motion_supported = request->is_motion && !request->jog &&
            (block->modal.motion == MotionMode_Seek || block->modal.motion == MotionMode_Linear) &&
            block->modal.feed_mode == FeedMode_UnitsPerMin &&
            (block->non_modal_command == NonModal_NoAction || block->non_modal_command == NonModal_AbsoluteOverride),
        .auxiliary_commands = request->has_auxiliary_commands
    };
    pmc_slave_packet_t packet;
    pmc_slave_motion_result_t result = pmc_slave_motion_packet(&motion, &packet);
    if(result == PmcSlaveMotion_Unsupported)
        return Status_GcodeUnsupportedCommand;
    if(result != PmcSlaveMotion_Remote && result != PmcSlaveMotion_NoMotion)
        return Status_GcodeValueOutOfRange;
    *handled = true;

    if(request->check_mode || result == PmcSlaveMotion_NoMotion)
        return Status_OK;
     if(!remote->online || remote->active_valid || remote->queue_count || waiting ||
         remote->independent_valid[0] || remote->independent_valid[1])
        return Status_IdleError;
    if(request->stage == MotionDispatch_Validate)
        return Status_OK;

    protocol_buffer_synchronize();
    if(ABORTED)
        return Status_IdleError;

    limits_soft_check((float *)block->values.xyz, data->condition);
    if(ABORTED || (state_get() & (STATE_ALARM | STATE_ESTOP)))
        return Status_IdleError;

    double travel_ms = fabs((double)motion.target - (double)motion.start) * 60000.0 /
        (double)(motion.rapid ? motion.max_rate_per_minute : min(motion.feed_per_minute, motion.max_rate_per_minute));
    double timeout = travel_ms * 3.0 + 10000.0;
    if(timeout > INT32_MAX)
        return Status_GcodeValueOutOfRange;

    waiting = true;
    completed = succeeded = false;
    if(!pmc_slave_bridge_submit(remote, &packet, now_ms(), (uint32_t)timeout, &waiting_sequence)) {
        waiting = false;
        return Status_IdleError;
    }

    if(data->execution_id)
        st_execution_effect_queued(data->execution_id);
    while(!completed && !ABORTED) {
        poll_remote(NULL);
        if(!protocol_execute_realtime())
            break;
    }
    waiting = false;

    if(!completed || !succeeded || ABORTED) {
        stop_requested = true;
        poll_remote(NULL);
        return Status_IdleError;
    }

    sys.position[axis] = packet.words[1];
    plan_sync_position();
    if(data->execution_id)
        st_execution_complete(data->execution_id);
    return Status_OK;
}

bool pmc_slave_dispatch_bind(pmc_slave_bridge_t *bridge)
{
    if(N_AXIS < 8 || !bridge || !bridge->transport.send || remote)
        return false;
    if(!task_add_systick(poll_remote, NULL))
        return false;

    remote = bridge;
    previous_dispatch = grbl.on_motion_dispatch;
    grbl.on_motion_dispatch = dispatch_motion;
    previous_event = bridge->on_event;
    previous_event_context = bridge->event_context;
    bridge->on_event = on_event;
    bridge->event_context = NULL;
    previous_state = grbl.on_state_change;
    grbl.on_state_change = on_state_change;
    previous_reset = grbl.on_reset;
    grbl.on_reset = on_reset;
    previous_jog_cancel = grbl.on_jog_cancel;
    grbl.on_jog_cancel = on_jog_cancel;
    previous_home = grbl.home_machine;
    grbl.home_machine = home_local;
#if VELOCITY_JOG_ENABLE
    static const direct_motion_remote_t backend = {
        .axes_mask = PMC_SLAVE_AXES_MASK,
        .move = dm_remote_move, .velocity = dm_remote_velocity,
        .stop = dm_remote_stop, .state = dm_remote_state
    };
    if(!direct_motion_bind_remote(&backend)) {
        grbl.on_motion_dispatch = previous_dispatch;
        grbl.on_state_change = previous_state;
        grbl.on_reset = previous_reset;
        grbl.on_jog_cancel = previous_jog_cancel;
        grbl.home_machine = previous_home;
        bridge->on_event = previous_event;
        bridge->event_context = previous_event_context;
        task_delete_systick(poll_remote, NULL);
        remote = NULL;
        return false;
    }
#endif
    return true;
}

#endif