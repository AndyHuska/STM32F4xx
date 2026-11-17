# PMC-02-AA Slave Bridge - Implementation Plan

Status: **draft for review** (2026-11-17). F4 only. No code written yet.

Related: [pmc_slave_bridge.md](pmc_slave_bridge.md) (current layers),
[PMC_SLAVE_V2_API_SPEC.md](PMC_SLAVE_V2_API_SPEC.md) (proposed v2 slave API),
[CLIENT_SUPERVISION_SPEC.md](CLIENT_SUPERVISION_SPEC.md),
[../plugins/VELOCITY_AXES_PLAN.md](../plugins/VELOCITY_AXES_PLAN.md).

## 1. Goal

Make the slave CPU's two axes (client D/E = grblHAL U/V, indices 6/7), its four
relays and its inputs usable from the main grblHAL firmware over USART2, with a
safe link (heartbeat/watchdog/link-loss policy), live status, G-code moves,
Direct Motion (DM) moves/jogs and velocity-axis (pump/conveyor) control.

Out of scope: slave firmware update (superseded, see PMC_LEGACY_BOOT_BUILD.md),
coordinated XYZ+U/V motion, and slave-axis homing in the initial implementation.

## 2. Current state

### 2.1 Layers that exist

| File | State |
|---|---|
| `pmc_slave_protocol.c/h` | Frame encode/decode, request allowlist (legacy commands 1-36 subset), reply classification, 8-word status decode. Native-tested (`tests/pmc_slave_protocol_test.c`). |
| `pmc_slave_bridge.c/h` | Transport-agnostic transaction engine: 4-entry queue + 1 active request, 2 independent-move slots (one per axis), heartbeat, online/link-timeout, status cache, version cache, events. Native-tested. |
| `pmc_slave_motion.c/h` | Single-axis G0/G1 -> legacy `MoveIndependent` (steps, steps/s, steps/s^2). |
| `pmc_slave_dispatch.c/h` | Binds a bridge to `grbl.on_motion_dispatch`, DM remote backend, state/reset/jog-cancel/homing hooks and a 1 kHz systick poll. |
| `pmc_slave_update_*` | Early update prototype; superseded, not wired. |

### 2.2 Transport and binding - **not implemented**

- No USART2 driver exists. `Src/serial.c` can open USART2 on PD5/PD6 (`SERIAL_PORT 21`) as a
  grblHAL text stream, but has no RTS/CTS and is interrupt-per-byte (2.625 Mbaud = ~262 kB/s).
- `pmc_slave_dispatch_bind()` is never called. `board_init()` only sets up the PD0 output gate and PD1.
- Consequence: today nothing is sent to the slave; the 8-axis build drives U/V on temporary
  main-CPU spare pins (PC7/PE6/PE7, PC8/PE9/PE10, see pmc_02_aa.md).

### 2.3 Heartbeat and link state

- Bridge sends `Heartbeat` (3) when nothing else was sent for `heartbeat_ms` (default 1000 ms).
- `online` is set by any valid received frame (except version), cleared after `link_timeout_ms`
  (default 2000 ms) without RX, on transport disconnect, or on any transaction timeout.
- Legacy slave facts (from `octo_fw/.../PMC-02-AA`, `#ifdef PROC_SLAVE`):
  - Replies to heartbeat with an empty-payload frame (SlaveComms.c ~L60).
  - Watchdog: 5000 ms without any valid packet -> disables output buffers, cancels motion,
    stops jogging, **disables status reports** (Watchdog.c ~L28, L50-61).
  - Original master heartbeat interval: 500 ms; master flagged "slave not responding" after 2000 ms.
- Nothing above the bridge reacts to `Online`/`LinkLost` except failing in-flight jobs.

### 2.4 Status packets

- Legacy `StatusReport` (25), 8 words: system_ok, D pos (steps), E pos (steps), reserved,
  jogging, motion_inputs (raw bits), D vel (steps/s), E vel (steps/s).
- Periodic only after `EnableStatusReport` (5) [interval]. **The bridge never sends it**, and the
  slave watchdog clears it, so it must be re-sent after every (re)connect.
- `pmc_slave_dispatch.c` copies status words 1/2 into `sys.position[6/7]` and uses velocity for
  DM remote rates. Parser position is only resynced by DM's IDLE_SYNC path.
- `motion_inputs` and `jogging` are decoded but unused. Bit layouts are not yet confirmed.

### 2.5 G-code

- `dispatch_motion()` handles single-axis U or V G0/G1 (G90/G91, G53, units/min feed) as
  `MoveIndependent | SYNC` and blocks the parser (`protocol_buffer_synchronize()` then waits for
  the completion reply). `[EXEC:N]` is reported on completion. Mixed XYZ+UV, arcs, raw `mc_line`
  (e.g. G28/G30 including U/V) are rejected.
- It rejects with `Status_IdleError` if the bridge has *anything* queued/active (including a
  relay/jog/heartbeat-unrelated request) instead of waiting.

### 2.6 Direct Motion

- DM remote backend (`dm_remote_move/velocity/stop/state`) latches requests; `dm_poll()` submits
  `MoveIndependent | SYNC` (per-axis independent slot) or `JogVelocity` (27, steps/s).
- DM "stop axis" for a remote axis stops **both** (legacy `StopMotion` 28 is slave-global).
- DM accel for remote jogs is ignored (legacy jog uses `AxisKinematicSetup` accel).

### 2.7 Velocity axes plugin

- Backend interface is ready (`velocity_axes_register_backend()`); the local backend excludes
  axes bound to DM's remote backend, so U/V cannot be reserved until a slave backend exists.
- No slave backend yet. Legacy `JogVelocity` (27) has no per-command accel/decel.

### 2.8 IO

| Item | Today | Owner per legacy/v2 docs |
|---|---|---|
| Relays 1-4 (P0-P3) | Main CPU PWM, TIM9-12 | Main |
| Relays 5-8 (P4-P7) | **Temporary main-CPU pins**, TIM1-4 | Slave relays 0-3 (`pmc_slave_relay_id(4..7)`) |
| General outputs P0-P4 (M64/M65) | Main CPU pins PC14/PC15/PD7/PE4/PE15 | Main CPU; the original master does not forward these writes. |
| D/E home/limit/error inputs | Temporary main spare pins (not real wiring) | Slave (`motion_inputs`) |

Legacy `RelayDriver` (11) payload is 4 words and `RelayDriverDualPulseAndHold` (36) 6 words; their
time units/semantics still need confirming against the legacy source before mapping M1010/M1011.

### 2.9 Stop / safety

- `poll_remote()` treats feed hold, stop, reset and motion cancel (and state HOLD/ALARM/ESTOP,
  `on_reset`, `on_jog_cancel`) as "stop": it calls `pmc_slave_bridge_disconnect()` (drops every
  queued request, marks the link offline) and then queues a global `StopMotion`.
- Client supervision (alarm 22) sets `EXEC_RESET` directly, which `poll_remote()` sees.

### 2.10 Homing

- `home_local()` refuses any cycle containing U/V. The legacy shared command parser has handlers
  for `HomingRoutine` (10) and `HomingSetup` (14), but a search of the original master's command
  and queueing paths found no forwarding of either command to the slave. The homing branches in
  the master's slave-reply handler are empty. Slave homing capability and actual configured axes
  are not established by the presence of those handlers.
- Treat U/V as non-homing axes initially; do not send either homing command. Exclude them from
  all-axis homing and reject explicit U/V homing with a clear error.

### 2.11 Problems found during this review

1. **Timer conflict (regression, current 6/8-axis builds)**: `STEP_INJECT_ENABLE` (forced by the
   velocity plugin) claims the first free generic timer at driver init (TIM1), and the velocity
   plugin claims the next (TIM2). Relays P4 (TIM1 CH1) and P5 (TIM2 CH4) are claimed lazily on first
   use, so `M1010/M1011 P4/P5` are expected to fail now (inferred from the timer table order, not yet
   confirmed on hardware). Moving P4-P7 to the slave removes this on PMC, but the
   F4 timer allocator should not hand out PWM-pin timers first.
2. Stop handling disconnects the link and kills all queued work, including relay/config requests,
   and stops both slave axes on any feed hold - incompatible with per-axis velocity-axis options.
3. G-code U/V moves fail spuriously while unrelated bridge traffic is pending (2.5).
4. Status reporting is never enabled; positions/velocities stay stale.
5. Client supervision only considers machine motion states; a pump/conveyor running while the
   machine is Idle does not arm the watchdog.
6. Bridge heartbeat (1000 ms) is slower than the original master (500 ms); this is independent of
  the client-supervision watchdog and requires validation under congestion and link loss.

## 3. Decisions

| # | Topic | Decision |
|---|---|---|
| 3.1 | Slave firmware | Prefer unchanged legacy slave firmware v1.1.1 or later if it satisfies the required behavior. The user controls that firmware, so propose only necessary, explicitly agreed changes. Version compatibility and behavior must be verified; do not assume all later versions are compatible. |
| 3.2 | General outputs P0-P4 | Main CPU only. The original master never forwards GeneralOutputWrite to the slave. |
| 3.3 | Slave link loss | Raise a hardware-failure alarm and stop the machine, including when idle: slave axis and relay control is no longer reliable. Invalidate remote state; reconnection must not automatically clear the alarm or resume motion. Final alarm identifier and recovery procedure require design. |
| 3.4 | Feed hold | Ignore feed hold for slave axes, including reserved velocity axes. Do not send slave stop solely due to feed hold; explicitly handle its interaction with existing per-axis hold options. Cancel, reset, alarms and E-stop are separate events and require defined behavior. |
| 3.5 | Main-to-slave heartbeat | The bridge maintains heartbeat regardless of machine state or executing work; bound scheduling latency under traffic. This is separate from client-to-main supervision, whose idle-velocity policy remains open. |
| 3.6 | U/V homing | Do not home U/V in the initial implementation; exclude them from automatic cycles and reject explicit requests. No master forwarding was found in the original code. |
| 3.7 | Slave inputs | Expose D/E home/limit/error as read-only auxiliary inputs with freshness/unknown handling. Do not infer a hard-limit safety policy from telemetry. |
| 3.8 | Temporary F4 pins | Remove the temporary U/V and P4-P7 main-CPU mappings. If the bridge is disabled, uninitialized or not ready, reject U/V motion and remote relay commands; never fall back to those pins. Local X/Y/Z/A/B/C behavior is unchanged. |

## 4. Implementation phases

### 4.0 Fix timer allocation (independent, do first)

Assess timer allocation against the actual local P0-P3 PWM assignments and velocity-axis timers.
The P4/P5 conflict in 2.11 concerns temporary pins that will be removed; do not fix those mappings
as a prerequisite. Protect actual local PWM timer resources and test before claiming the issue is fixed.

### 4.1 USART2 transport

- New `boards/pmc_slave_uart.c`: USART2 PD5/PD6, RTS PD4 / CTS PD3 hardware flow control,
  2.625 Mbaud (APB1 42 MHz / 16), RX circular DMA into SRAM (not CCM), TX DMA from a copied
  frame buffer, IRQ only for idle-line/half/complete. Implements `pmc_slave_transport_t`
  (`send` copies; `connected` = always true or CTS-based).
- Foreground drain of RX DMA into `pmc_slave_bridge_receive()` from the existing systick poll.
- `board_init()` / a late-init task: create bridge, bind transport, call `pmc_slave_dispatch_bind()`.
- Build flag `PMC_SLAVE_BRIDGE_ENABLE` (8-axis envs only). Guard U/V and relay dispatch independently
  of successful bridge initialization, including disabled builds; no temporary-pin fallback.

### 4.2 Link management

- Service heartbeat independently of active requests, planner load and motion state. Start with
  the original master's 500 ms interval and 2000 ms receive timeout as testable defaults; prioritize
  heartbeat/stop over ordinary traffic and measure worst-case latency and CTS backpressure.
- Establish application readiness and verify the legacy protocol/version before actuation. Query
  `Version`, configure (4.3), then enable status (interval to be verified). Version text alone does
  not prove application readiness: the bootloader can advertise the application version.
- On `LinkLost`: ready=false, remote positions, rates, inputs and relay shadows unknown; raise the
  hardware-failure alarm and stop local motion. Reconnect for diagnostics, but do not clear the
  alarm, enable outputs or resume motion automatically. Define startup detection/grace and recovery.
- Report link state: `[SLAVE:state,version]` in `$I` / on change, plus a status-report flag.

### 4.3 Configuration sync

- From applicable U/V settings: evaluate `AxisKinematicSetup` (15), `SoftLimitsSetup` (12),
  `SoftLimitsToggle` (20) and polarity (17/18) against the actual legacy source before sending.
  Do not send `HomingSetup` (14).
- Re-send on ready and on relevant setting changes (`on_settings_changed`), only while idle.
- Unit conversions in one place; reject out-of-range values with a reported error.

### 4.4 Status integration

- Status -> `sys.position[6/7]` (already), `|AV:` via DM/velocity backends, `jogging`, inputs.
- After remote motion ends (G-code completion, DM idle, velocity axis stop) resync parser and
  planner positions for U/V.
- Stale status (> N ms) marks U/V rates/inputs unknown.

### 4.5 Stop and safety rework

- Replace "disconnect + global stop" with event-specific handling. Feed hold must not stop slave
  axes; cancel, reset, selected alarms and E-stop must have separately specified semantics.
- Preserve communication and heartbeat while cancelling appropriate work. Legacy `StopMotion`
  is global: verify whether a per-axis velocity-zero command provides the requested controlled
  stop and document unavoidable effects on the other axis. Do not silently stop an unrelated pump.
- Link loss follows 3.3; stop transmission to a lost link is not proof the slave stopped. The slave
  watchdog and physical interlocks remain independent protections.

### 4.6 Velocity-axes slave backend

- Backend claims U/V as remote-owned even when offline; availability and readiness are distinct.
  `set_target` latches signed units/min, foreground `poll()` submits `JogVelocity` (27), and
  `get_rate()` uses fresh slave status. Return or report failures rather than silently drop them.
- Verify legacy acceleration/deceleration and per-axis stop behavior before promising `R`, configured
  decel or independent stops. If unsupported, identify the smallest slave firmware extension needed.
- DM must continue to reject reserved axes and must never fall back to local U/V step pins.

### 4.7 G-code and DM refinements

- `dispatch_motion()`: wait (with realtime processing) for bridge capacity instead of
  `Status_IdleError` when only non-motion traffic is pending.
- Exclude U/V from automatic homing and reject explicit remote homing per 3.6.
- Verify which per-move and jog acceleration parameters legacy firmware honors before syncing them.

### 4.8 IO forwarding

- Route relay channels P4-P7 (`M1010/M1011/M1012`) to slave `RelayDriver`/`RelayPulseHold` once
  the payload semantics are confirmed; shadow state reported as "unknown" while not ready.
- Slave inputs per 3.7; keep general output writes on the main CPU per 3.2.

### 4.9 Tests

- Extend `tests/pmc_slave_protocol_test.c` for new link-state, stop policy and backend latching.
- Bench sequence: link up/version/status with outputs inhibited; verify heartbeat during idle,
  motion, queue saturation and CTS stall; test unplug/link loss while idle and active, hardware
  failure alarm and no automatic restart; verify feed hold leaves slave axes running; test
  reset/E-stop independently; reject U/V and slave relay commands whenever unavailable.
- Verify remote inputs and stale-state reporting, GPOs remain local, no U/V homing packet is sent,
  and velocity U can run while XYZ G-code and DM run. Exercise watchdog and physical interlocks.

## 5. Order of work

Verify the legacy contract and resources -> fail-closed ownership and transport -> heartbeat/link
management and stop policy -> configuration/status -> velocity/DM/G-code -> IO. Keep outputs
inhibited during initial bench testing; never flash or enable live hardware without authorization.

## 6. Open items to confirm in the legacy source

- `motion_inputs` and `jogging` bit layouts; `EnableStatusReport` interval units.
- `RelayDriver` (11) and `RelayPulseHold` (36) payload semantics/units.
- `AxisKinematicSetup` (15) word 3 meaning and polarity (17/18) words.
- Whether `StopMotion` decelerates (reported: uses kinematic accel) and how jogs respond to it.
- Version 1.1.1 identification and installed slave behavior, independent stop and acceleration
  capabilities, and minimum changes if any are needed. Client-supervision behavior while only
  velocity axes are active is separate from the slave heartbeat and remains undecided.
