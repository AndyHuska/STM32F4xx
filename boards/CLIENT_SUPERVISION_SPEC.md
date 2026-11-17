# Client Motion-Supervision Watchdog

Applies to the PMC-02-AA F4 and Waveshare OpenH743I-C H7 builds only. Both
compile with `CLIENT_SUPERVISION_ENABLE=1` and
`CLIENT_SUPERVISION_TIMEOUT_MS=2000`. Other platform builds leave this feature
undefined/off. These definitions are scoped to those two PlatformIO
environments, not the shared defaults. F4 and H7 builds pass; no hardware
watchdog trip was induced during this change.

## Client Contract

While the controller reports an active motion state (Cycle, Jog, Homing, Hold,
or Safety Door pause), the client must send the realtime status byte `?` at
least once every 500 ms on the active Telnet/WebSocket grblHAL connection. It is an unnumbered
realtime byte, not a G-code line: do not prefix it with `N`, append a checksum,
or wait for a separate heartbeat acknowledgement. Continue parsing normal
unsolicited reports and command acknowledgements while sending it. Outbound
status reports, TCP keepalive packets, UI refreshes, and a still-open socket do
not count as received client activity.

The firmware refreshes its supervision timer on inbound application data from
the Telnet/WebSocket stream. `?` is the required idle-while-moving heartbeat;
ordinary received command bytes also demonstrate client activity. Firmware
starts a fresh 2-second window when it first observes an active motion state.
It does not require heartbeat traffic while idle.

Do not pause heartbeat while awaiting a motion acknowledgement or while the
planner is full. Realtime `?` can be sent while G-code remains queued. During a
client-controlled feed hold or Safety Door pause, continue heartbeats. If the application intentionally
stops communicating for more than 2 seconds during motion, supervision will
trip by design.

## Timeout Behavior

Silence for 2,000 ms during Cycle/Jog/Homing/Hold/Safety Door latches
`Alarm_ClientTimeout` (code 22) and requests the core reset/cancel path. This cancels queued planner
motion and stops plugin motion through the reset path. The current DirectMotion
plugin's ISR sees the alarm/reset and stops its local profile; the F4 slave
bridge queues its legacy slave-global StopMotion. A lost physical link cannot
guarantee the slave received that stop. Hardware E-stop/interlocks remain
independent and mandatory for safety.

The watchdog does not clear because a socket reconnects or a heartbeat resumes.
The client must stop the job, report the interrupted run, and require operator
inspection/re-home as appropriate. Re-arm requires explicit controller reset
and alarm unlock; do not automatically resume or resend the last motion command.
After re-arming, restart the stream from a known safe point.

No heartbeat response is a guaranteed acknowledgement. A status report may
follow `?`, depending on current reporting settings, but supervision is refreshed
from the received byte. On alarm 22, stop enqueueing work immediately, keep the
reader active to receive the alarm/reset response, and do not treat later `ok`
responses as evidence the aborted job completed.

## Verification Checklist

- Confirm `$EA`/alarm enumeration shows alarm 22 as client supervision timeout.
- While idle, leave heartbeat absent longer than 2 seconds: no watchdog alarm.
- Run a safe unloaded motion and send `?` every 500 ms: motion continues.
- During safe motion, stop all inbound application bytes: within 2 seconds the
  controller reports alarm 22 and aborts/cancels motion.
- Reconnect/send `?`: alarm remains; motion does not resume.
- Explicitly reset/unlock, then start a new controlled test.
- Repeat on each enabled transport (Telnet and WebSocket) and board.
- Test feed hold, G-code queue backpressure, direct-motion local axes, remote
  slave link loss, and alarm clear separately. Verify external hardware E-stop
  independently; software timeout is not its substitute.
