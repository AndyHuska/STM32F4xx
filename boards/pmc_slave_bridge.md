# Legacy PMC Slave Bridge

The authoritative v2 implementation contracts are now
[PMC_SLAVE_V2_API_SPEC.md](PMC_SLAVE_V2_API_SPEC.md) and
[CLIENT_SLAVE_UPDATE_SPEC.md](CLIENT_SLAVE_UPDATE_SPEC.md). They supersede
conflicting prototype behavior below. The streamed update endpoint is not yet
implemented on the main CPU; the user will implement/install the v2 slave code.

Status: protocol/transaction/motion layers are native-tested. The shared core
dispatch hook and F4 adapter are compile-validated. No USART transport is
enabled, no slave commands are sent, and
no running firmware was flashed for this work. Temporary main-CPU U/V and
relay P4-P7 mappings remain in place.

## Source Of Truth

Original project: `C:/Users/Andy/gitlab/octo_fw/STM32F4 Projects/PMC-02-AA`.
Command scope and framing were checked against
`motion_server/CommonCommandParser.c`, `SlaveComms.c`, `MasterSlaveComms.c`,
`motionserver.h`, `motion/Vector3Dt.h`, `PathManager.c` and `RelayPortDefs.h`.

The normal legacy link is USART2 at 2,625,000 baud, 8-bit data, one stop bit,
with RTS/CTS flow control. TX/RX are PD5/PD6; RTS/CTS are PD4/PD3. These pins
remain unassigned in the grblHAL board map. This is not an SPI transport.

The wire frame is little-endian: four 32-bit header fields (magic 0x55555555,
sequence, command/flags, payload byte count), followed by 32-bit payload words.
The bounded decoder uses the legacy 540-byte transport limit, not the larger
1024-byte generic packet union. It rejects oversized and unaligned lengths and
resynchronizes magic after invalid headers. The legacy frame has no checksum;
do not infer CRC protection from magic/length validation.

## Layers

- `pmc_slave_protocol.h/.c`: exact frame encoding/decoding, request allowlist,
  reply classification, status translation and address helpers. No HAL coupling.
- `pmc_slave_bridge.h/.c`: caller-supplied nonblocking transport, a four-entry
  queue, one active transaction, sequence matching, heartbeat, cached status
  freshness, timeout/link-loss events. No retries of uncertain commands.
- `tests/pmc_slave_protocol_test.c`: PC-native protocol/fake-transport tests.

`pmc_slave_axis_id(6/7)` maps main D/E (grblHAL U/V indices) to legacy slave
A/B IDs 3/4. Other axes are rejected. `pmc_slave_relay_id(4..7)` maps the
main relay namespace to slave channels 0..3. The packet builder expects local
slave IDs; it does not silently convert them. Payload integers retain their
legacy meaning: unit conversion belongs at the eventual plugin frontend.

## Command Coverage

| Command | ID | Payload words | Treatment |
| --- | --- | --- | --- |
| Disconnect | 1 | 0 | Device disconnect policy; not auto-sent |
| Echo | 2 | 0..131 | Discovery/reply framing |
| Heartbeat | 3 | 0 | Link keepalive |
| EnableStatusReport | 5 | 0 or 1 | Legacy reporting interval |
| SysReset | 6 | 0 | Explicit reset only |
| MotionInputRead | 7 | 0 | Raw motion-input bits |
| HomingRoutine | 10 | 0 | Slave-global homing |
| RelayDriver | 11 | 4 | Local relay channel 0..3 |
| SoftLimitsSetup | 12 | 4 | Local axis 3/4 |
| MoveIndep | 13 | 4 | Local axis 3/4 |
| HomingSetup | 14 | 5 | Local axis 3/4 |
| AxisKinematicSetup | 15 | 4 | Ordinary velocity/acceleration limits |
| AxisInputLevelSetup | 17 | 3 | Local axis 3/4 |
| AxisOutputLevelSetup | 18 | 3 | Local axis 3/4 |
| JoggingScaleIndependent | 19 | 2 | Local axis 3/4 |
| SoftLimitsToggle | 20 | 1 | Slave-global toggle |
| GeneralOutputWrite | 24 | 3 | Legacy address/bit/value |
| StatusReport | 25 | 8 | Incoming telemetry only |
| JoggingVelocityIndependent | 27 | 2 | Local axis 3/4 |
| StopMotion | 28 | 1 | Legacy handler ignores payload; stops slave globally |
| BootloaderCheckVer | 33 | 0 request / 16 reply | Version query only |
| RelayDriverDualPulseAndHold | 36 | 6 | Local relay pair 0..3 |

Excluded deliberately: MoveCoordinated (4), all XYZ jogging (8/26), MoveArc
(22), MoveBlended (31), AxisAccelProfileSetup (34). Some are still compiled
into the old parser for the slave, but are outside the requested abstraction.
Unsupported enum-only commands (StartupSequence, HomingReset, IdleDelay,
DebugOutput, InterlockSetup) are not advertised without verified slave handlers.

FirmwareUpdate (29) and BootloaderRun (32) are not allowed by this initial
bridge. The application FirmwareUpdate branch is a stub/failure, while
`SBOOTLOADER` has a separate update protocol beginning with 123456789 and
subsequent flash blocks. Update priming, erase/write/finalization and recovery
must be implemented/tested separately; ordinary transactions must not trigger it.

## Transactions And Status

Submission success means queued locally, not accepted/executed by the slave.
The transport's `send` success means it copied/queued the entire frame. Events
then distinguish Sent, Reply, Status, Version, Online, LinkLost and Timeout.
Callers classify Reply with `pmc_slave_reply_kind()` and inspect the payload.
Never equate `PmcSlaveEvent_Sent` with motion completion.

Normal independent moves can return an immediate acknowledgement. A request
with the sync flag 0x00010000 can defer its reply until completion. The bridge
keeps such a transaction active until the completion flag, a rejection or its
caller-selected timeout. Unmatched asynchronous completion events are exposed
but must be correlated by the frontend. Timeout invalidates the active request,
drops remaining queued work, and marks status/link unavailable; it does not
replay motion or assert that the physical slave stopped.

Timeout is end-to-end from submission, including local queue time, and must be
1..INT32_MAX milliseconds. Wraparound is supported. Default heartbeat/link
freshness intervals are 1000/2000 ms; configure them against the slave's actual
watchdog/reporting behavior before enabling the link. A fresh status packet is
eight words: system-ok, A position, B position, reserved C position, jogging,
motion-input bits, A velocity, B velocity. Status getter rejects stale/offline
data. Version replies are copied into a bounded 65-byte terminated string.

Version command 33 (`BootloaderCheckVer`) is verified in both application and
slave bootloader request handlers: send an empty payload and receive 64 bytes
of `ProductNameAndVersion`. The `SBOOTLOADER` periodic path also sends this
packet unsolicited while waiting for the master, with a slave-generated
sequence number. An automatic announcement was not found in the ordinary
application startup path, so discovery must not depend on receiving one.

The bridge now accepts both requested and unsolicited version frames, caches
the terminated text and emits a Version event. Only matching command/sequence
completes an outstanding version query; a boot/version message cannot complete
an unrelated motion. Version alone does not mark the application online because
the bootloader can advertise the application's stored version string too.
It is identification, not proof that the application is ready for actuation.

The legacy status packet contains no relay/general-output readback. A future
frontend can show last acknowledged command state, but must identify it as a
shadow and mark it unknown when the link is lost. Actual hardware output-state
telemetry would require a slave firmware/protocol extension.

## Integration Contract

All bridge methods/event callbacks run in the main-CPU foreground context,
not inside the UART IRQ. Callbacks must not recursively feed RX or reset the
bridge. Queue work from callbacks only with lifecycle/reentrancy considered.
The UART interrupt should only enqueue bytes; foreground poll feeds the decoder.
Transport send must copy data before returning: the bridge reuses its frame
buffer and the caller can allocate the bridge in CCM. A DMA adapter must keep
its copied transmit buffer in DMA-accessible SRAM, never use CCM directly.

The bridge is intentionally not plugged into core G1 planning or M1011 routing
yet. That avoids silently changing the current temporary resources while the
legacy wire contract is being validated. Initial live integration should first
check version/echo/status and flow control, with remote actuation disabled.
Homing/reset/stop and hold/cancel/E-stop coupling require an explicit policy.

### Initial Integration Requirements

User decisions: main feed hold/cancel/reset/E-stop must request a stop of both
slave axes; eventual live integration should route axis/relay commands rather
than remain discovery-only. The current foundation is still transport-inactive.

Preferred frontend is standard G-code and the existing direct-motion jogging
plugin, not a separate vendor move-command family. The pinned core has
homing/jog notification hooks but no early external-axis motion dispatch hook.
A small shared-core extension is therefore required before ordinary U/V motion
can be routed instead of sent to the main stepper engine.

The viable initial standard-code scope is slave-only G0/G1 U/V and independent
jogging. Mixed XYZ+U/V coordinated blocks cannot preserve coordinated feed,
timing and execution guarantees with the legacy independent-move API; reject
them instead of silently splitting them. Per user scope, coordinated XYZ+U/V
support will not be implemented; it is not a future-work requirement.

Legacy HomingRoutine starts the slave-wide cycle; selected-axis $HU/$HV cannot
simply be forwarded with an axis number. Decide the supported homing scope and
validate axis selection before integrating with normal grblHAL homing. Remote
execution acknowledgements and telemetry must update the correct execution IDs
and axis state, not be treated as locally scheduled pulses. Confirm legacy
position/rate units at the frontend boundary before translating millimeters.

## Core Dispatch Implementation

The following supersedes the initial hook requirements above. The nullable
`grbl.on_motion_dispatch` hook is now identical in both core checkouts. Parsed
validation occurs after parser checks and coordinate conversion, before normal
execution. Execute handles claimed motion instead of local planning. With no
handler registered, existing G-code/jog/G64 paths remain in use.

The callback returns a status and writes `handled`. Validation receives no
planner data and must not send commands or mutate machine state. It must reject
unsupported combinations before execution. Execute receives planner data and
the execution ID; claimed motion must be handled or fail, never silently fall
back to local pulses. Requests include parser block, target/start, axis words,
jog/check-mode and auxiliary-command flags. Check mode must not send motion.
Raw mc_line requests have `raw=true`, `block=NULL`, and an axis mask derived
from target versus planner position; the adapter rejects raw remote calls.

`pmc_slave_motion.h/.c` provides tested single-axis routing and checked position,
feed and acceleration conversion. `pmc_slave_dispatch.h/.c` binds a caller's
bridge to the new hook and registers polling, position/status handling and stop
callbacks. Binding requires an eight-axis build and a transport send callback.
No board initialization calls this binding yet; USART2 remains unconfigured and
the temporary main-CPU U/V/relay mappings remain active.

The first adapter handles only single-axis slave-only G0/G1 under G94, without
M-code/accessory/macro effects in the same block. It drains local motion, checks
soft limits, sends a legacy synchronous request, waits for completion, and
reconciles parser/planner position and execution accounting. Parser ack is
therefore deferred until completion: a short fixed timeout or unbounded command
burst is not an appropriate client contract for this initial backend.

Targets convert mm to signed steps, feed/max rate mm/min to steps/s, and
acceleration mm/min^2 to steps/s^2. The old independent generator divides
stepFrequencyMax by velocity, confirming the seconds-based rate boundary.
Finite/range checks reject unrepresentable values. Step-equivalent targets are
no-ops completed locally because the old slave's no-op branch does not return
the deferred completion flag. Fresh slave status updates reported U/V steps.

Main hold/cancel/reset/E-stop events request a slave-global stop, with explicit
realtime flags checked while waiting even if the main planner is idle. Requests
run in foreground, not the interrupt callback. A lost physical link cannot
guarantee the slave stopped. Relay/M1011 routing, velocity-report merging,
mid-move feed overrides, USART2 and remote $J/direct-motion jogging/homing still
require integration before live activation. Raw remote movement and default or
remote homing are rejected after binding rather than sent to temporary GPIOs.
The mixed XYZ+U/V and dual-remote-axis cases are deliberately unsupported.

Native tests pass for protocol, bridge, offline stop, routing, units, rapid/feed
limits, negative values, overflow, unsupported/accessory motion and quantized
no-ops. The core/parser callback adapter has been compile-validated only, not
runtime-tested on a slave. F4 eight-axis and H7 Waveshare builds passed; neither
board was flashed and the bridge is not enabled in the running firmware.

## Validation

From the F4 repository using the installed MSYS2 native GCC:

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
gcc -std=c11 -Wall -Wextra -Werror tests/pmc_slave_protocol_test.c boards/pmc_slave_protocol.c boards/pmc_slave_bridge.c boards/pmc_slave_motion.c -o "$env:TEMP/pmc_slave_protocol_test.exe"
& "$env:TEMP/pmc_slave_protocol_test.exe"
```

Tests passed for exact bytes, signed extremes, malformed/bounded/fragmented
frames, repeated frames, command/axis/relay exclusions, status mapping,
acknowledgement versus completion, busy transport, sequence mismatch, queue
capacity, version data, stale telemetry, timeout/no replay, wraparound and link
loss. The target compiler also passed strict syntax checks. The normal
eight-axis firmware build passed with no active bridge instance linked in.