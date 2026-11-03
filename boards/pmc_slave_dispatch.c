#include "driver.h"

#if defined(BOARD_PMC_02_AA)

#include "pmc_slave_dispatch.h"
#include "pmc_slave_motion.h"
#include "grbl/motion_control.h"
#include "grbl/protocol.h"
#include "grbl/state_machine.h"
#include "grbl/task.h"
#include <math.h>

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

static uint32_t now_ms(void)
{
    return hal.get_elapsed_ticks();
}

static void on_event(void *context, pmc_slave_event_t event, uint32_t sequence,
                     const pmc_slave_packet_t *reply)
{
    (void)context;
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
    }
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
    if(!remote->online || remote->active_valid || remote->queue_count || waiting)
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
    return true;
}

#endif