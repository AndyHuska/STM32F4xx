# PMC-02-AA Slave API v2.0

Status: proposed normative implementation contract, 2026-10-31. The user will
implement/install the v2 slave bootloader and application. This specification
supersedes conflicting behavior in the legacy parser and early bridge/update
prototypes. No v2 bootloader or application has been installed by this work.

Client/main-CPU streaming contract: [CLIENT_SLAVE_UPDATE_SPEC.md](CLIENT_SLAVE_UPDATE_SPEC.md).
Earlier bridge implementation notes: [pmc_slave_bridge.md](pmc_slave_bridge.md).

## Scope And Ownership

The slave owns two independent axes (D/E, represented as U/V by grblHAL), four
PWM relays, and five general-purpose outputs. Internal motion remains in steps
and steps/second. XYZ, coordinated XYZ+U/V, arcs/blending and acceleration
profiles are outside scope. Do not silently split coordinated blocks.

Main grblHAL owns G-code parsing, units/offset resolution, client settings and
command/execution correlation. The slave owns its hardware limits, motion,
homing, output state and firmware flash. Neither device may report accepted
commands as completed merely because a UART frame was queued.

Main hold/cancel/reset/E-stop requests a stop of both slave axes. This protocol
is not a hardware E-stop guarantee: a broken link requires a local slave
watchdog/safe-output policy independent of the main CPU.

## Transport And Envelope

USART2, 2,625,000 baud, 8N1, RTS/CTS hardware flow control. Legacy wiring:
PD5 TX, PD6 RX, PD4 RTS, PD3 CTS. Confirm actual cross-connections and polarity
before enabling hardware. Use DMA-accessible SRAM for DMA buffers, not CCM.

All fields below are 32-bit little-endian words. A frame is:

| Offset | Field |
| --- | --- |
| 0 | Magic 0x55555555 |
| 4 | Sequence, uint32 |
| 8 | Command plus defined flags |
| 12 | Payload length in bytes, uint32 |
| 16 | Payload |

Maximum frame size is 540 bytes; payload maximum is 524 bytes and must be
word-aligned. Reject lengths before allocating/copying. Preserve fragmented
frames; resynchronize after malformed frames. No implicit C-struct packing.
Payload word notation is zero-based. Signed step positions are int32 two's
complement. Float fields are IEEE-754 binary32 bit patterns copied as uint32,
not numeric casts. Reject nonfinite/invalid numeric values.

Low 16 command bits identify the command. Flag 0x00010000 identifies a
completion reply for commands that define one. Other flags are reserved and
must be rejected. Update requests use no flags. Responses echo request command
and sequence except explicit asynchronous status/boot notifications.

The legacy envelope has no frame CRC. Update blocks and images have CRCs below;
framing validation is not authentication. Use an isolated trusted maintenance
network. Do not describe Telnet or CRC as secure image authentication.

## Command Allocation

Do not renumber existing commands 0-36. Commands 37-42 match the current update
prototype; commands 43-54 extend it. Application and bootloader must not
interpret the same update opcode as ordinary motion.

| ID | Name | Receiver |
| --- | --- | --- |
| 33 | VERSION (legacy BootloaderCheckVer) | Both |
| 37 | INFO | Both |
| 38 | UPDATE_BEGIN | Bootloader |
| 39 | UPDATE_BLOCK | Bootloader |
| 40 | UPDATE_COMMIT | Bootloader |
| 41 | UPDATE_ABORT | Bootloader |
| 42 | UPDATE_REBOOT | Bootloader |
| 43 | ENTER_BOOTLOADER | Application |
| 44 | CAPABILITIES | Application |
| 45 | AXIS_UNITS_CONFIG | Application |
| 46 | EXTENDED_STATUS | Application |
| 47 | MOVE_UNITS | Application |
| 48 | JOG_UNITS | Application |
| 49 | HOME_AXES | Application |
| 50 | Reserved | Neither |
| 51 | RELAY_WRITE | Application |
| 52 | GENERAL_OUTPUT_WRITE | Application |
| 53 | STOP_AXES | Application |
| 54 | OUTPUT_ENABLE | Application |

Legacy maintenance controls may remain: StopMotion 28, SysReset 6 and VERSION
33. BootloaderRun 32 is optional migration compatibility, never a heartbeat
side effect. Application update writes must return unsupported, never erase.
Disable the unsafe legacy FirmwareUpdate 29 implementation in the v2 loader.

## Discovery And Readiness

VERSION request has no payload. Reply has exactly 64 payload bytes containing
ProductNameAndVersion, zero-padded or terminated within that buffer. The main
must bound string reads and add its own terminator. A bootloader may send this
unsolicited, with its own sequence; it cannot complete an unrelated request.
The legacy SBOOTLOADER advertises the stored application version, so VERSION
alone is NOT proof of application mode or API compatibility.

INFO request has no payload. Reply is exactly seven words:

| Word | Meaning |
| --- | --- |
| 0 | Update/discovery protocol major: 2 |
| 1 | Mode: 0 application, 1 bootloader |
| 2 | Application base: 0x08010000 |
| 3 | Application exclusive limit: 0x08080000 |
| 4 | Update block bytes: 512 |
| 5 | Active update session, or 0 when none |
| 6 | Operational API major: 2 in v2 application, 0 in bootloader |

Unsupported/malformed INFO is a hard stop before erase. Main must request INFO
with a fresh sequence after boot entry; a version announcement is insufficient.

CAPABILITIES request has no payload. Reply is eight words: major=2, minor=0,
feature_mask, axis_count=2, relay_count=4, general_output_count=5,
update_api_major=2, max_frame_bytes=540.

Feature bits: 0 units configuration; 1 extended status; 2 unit moves; 3 unit
jogging; 4 selected-axis homing; 5 acceptance/completion correlation; 6 commanded
output/GPIO readback; 7 selected-axis stop. Full v2.0 requires mask 0x000000FF.
Physical actuator sensing is not implied by bit 6.

Remote actuation requires application INFO with operational API 2, validated
CAPABILITIES, and fresh/configured axis state. Legacy discovery/update can be
used for migration; legacy operational firmware must not silently pass this gate.
On reset, link loss or boot-ID change invalidate configuration readiness and
commanded-state shadows until renegotiation. Updating does not automatically
enable motion or hardware output gates.

## Image And Flash Layout

This is a raw slave application BIN, NOT the main grblHAL image, ELF, HEX, or
combined bootloader image. Offset 0 corresponds to 0x08010000. Preserve the
512-byte legacy metadata prefix; vectors are at file offset 512 (0x08010200).
Size must be word-aligned, at least 520 bytes, at most 458,752 bytes. A final
short block must still be word-aligned. No hidden padding in the transport.

Before commit validate initial MSP is 8-byte aligned and in
(0x20000000, 0x20020000], and reset vector is Thumb and lies in
[0x08010200, 0x08010000 + image_size). This matches the legacy SRAM stack;
supporting a CCM stack requires an explicit loader/vector-policy revision.
Reject empty/erased metadata. Validate compiled flash density and linker map.

Reserve flash sectors 0-2 (0x08000000-0x0800BFFF, 48 KiB) for the v2 bootloader.
Reserve sector 3 (0x0800C000-0x0800FFFF, 16 KiB) as a commit journal. Application
sectors are 4-7. Loader code must fit 48 KiB and must not overlap the journal.
If it does not fit, revise the layout/specification before using the updater.
Remote operations never write bootloader code or change option bytes/RDP/WRP.
Protect bootloader sectors if desired, but the journal/application must be
writable. Initial JTAG installation and any option-byte changes are manual,
explicitly authorized maintenance operations; do not mass-erase automatically.

The journal invalidates boot eligibility before application erase. A valid
record at 0x0800C000 contains eight words: magic=0x43325650 (PV2C), format=2,
image_size, image_crc32, application_base, operational_api_major=2, session,
record_crc32. Record CRC is calculated over the first seven little-endian words
using the intended magic value. Write/verify words 1-7 before writing magic
word 0 LAST. On boot require record magic/CRC, bounds, vectors and complete
image CRC to pass before jumping to the application.

There is no rollback image in this single-bank layout. Interrupted updates must
leave a reachable, output-inhibited bootloader; recovery retransmits a complete
image with a new session. Header-last writing alone is not a durable commit
record and is insufficient for production boot eligibility.

## CRC Definition

CRC-32/ISO-HDLC: reflected polynomial 0xEDB88320, initial register 0xFFFFFFFF,
final XOR 0xFFFFFFFF; equivalently standard CRC32 with incremental API initial
value 0. Check vector ASCII 123456789 = 0xCBF43926.
Image CRC covers every byte of the exact BIN, including metadata and any linker
gaps. Block CRC covers only that block's data, not header/session/offset/CRC.
Journal CRC covers the bytes defined above. Do not assume STM32 hardware CRC
defaults implement this variant. No image CRC is embedded in the image itself.

## Boot Entry And Update Replies

ENTER_BOOTLOADER request: [session_nonzero, flags=0]. Require application idle,
no queued motion, and safe outputs. Reply [result, session, 0]. Inhibit outputs,
record a reset-persistent boot request, drain reply through UART transmission
complete (not just DMA complete), then reset. The loader consumes the request
and stays in maintenance mode. Reserve/document backup registers or a noinit
region without overlapping stack/application state. After power loss without
a valid committed image, stay in maintenance mode regardless of the marker.

For first migration from v1 application, the main may use legacy StopMotion 28
and SysReset 6 only under an explicit maintenance/bootstrap option, with
actuators disconnected/electrically inhibited. Do not automatically fall back
to legacy reset from an unsupported 43 during ordinary operation. A v2 loader
must already be installed through JTAG for either entry path to work.

Result codes for update/entry replies (NOT legacy boolean ACK semantics):
0 OK, 1 INVALID, 2 STATE, 3 OFFSET, 4 CHECKSUM, 5 FLASH, 6 IMAGE, 7 UNSUPPORTED.
Replies to 38-43 are [result, session, next_offset]; abort without a session
reports the affected session/offset. Validate command, sequence and session.
Legacy replies still use their legacy payload and 1=success; never conflate them.

## Streaming Update Sequence

UPDATE_BEGIN 38: [session_nonzero, image_size, image_crc32]. Require confirmed
bootloader mode, valid size, safe outputs and no conflicting session. Invalidate
the journal FIRST, then erase application sectors, reset offset to zero, and
reply [0, session, 0] only after successful erasure. Same session/size/CRC retry
is idempotent: return current offset without erasing again. A different active
session or changed manifest is a conflict, not permission to restart an erase.

UPDATE_BLOCK 39: [session, byte_offset, block_crc32, data...]. Data is 1..128
words, normally 512 bytes; only the final block may be shorter. Require offset
equals next_offset, aligned to 512, and data length equals min(512, remaining).
Verify CRC BEFORE any write. Header block offset 0 is cached in slave RAM until
commit; other blocks are written/readback-verified at base+offset. Reply only
after staging/write verification, with next_offset advanced by actual bytes.
Offset/length arithmetic must be overflow checked and satisfy end <= exclusive
limit; the final word at limit-4 is writable. Never silently truncate writes.

The immediately previous block may be retried after a lost ACK. If session,
offset, length and exact bytes match, return the same next_offset without
rewriting/advancing. Compare bytes, not CRC alone. Older blocks/future offsets
are rejected; no speculative multi-block receive queue is permitted.

UPDATE_COMMIT 40: [session]. Require all bytes received. Validate vectors,
metadata and full image CRC using cached header plus flash body. Then write and
verify the metadata header. Verify the complete flash image CRC again and write
the durable journal with valid magic LAST. Reply [0, session, image_size] only
after all checks. Do not reset here. Same committed session retry is idempotent.

UPDATE_REBOOT 42: [session]. Require a valid durable committed image. Reply
[0, session, image_size], drain UART fully, clear maintenance boot request and
reset. Boot validates the journal/image and enters the application. The main
then verifies application INFO, VERSION and CAPABILITIES. Do not report update
success from a reset acknowledgement alone.

UPDATE_ABORT 41: empty payload. Stop accepting blocks, invalidate uncommitted
session state, reply with current session/offset, and stay in maintenance mode
with outputs safe. It does not restore the erased old image. Do not invalidate
an already valid committed journal merely to abort a completed session.

Heartbeat in v2 bootloader is keepalive only: it MUST NOT launch the application
or reset the update parser. Version/INFO queries never erase or enable outputs.
Optional BootloaderRun 32 requires a valid journal, no active update, and a
fully drained reply. Malformed frames, timeouts and flash faults return errors
and remain recoverable; never call HardFault_Handler as an error-reporting path.

Session timeout after 60 seconds without an accepted client block must abandon
an uncommitted transfer safely. Retain bounded last-block retry metadata while
active. Main reset/link loss cannot make a partial image bootable. Firmware
must service UART/control/watchdog safely during erase/program operations.

## Operational Units And Commands

V2 public axis IDs are 0 D/U and 1 E/V; raw legacy API IDs remain 3/4. Relay IDs
are 0..3 and general-output IDs 0..4. Do not reuse main-global IDs on v2 wire.

AXIS_UNITS_CONFIG 45 request, eight words: [axis_id, units_tag, steps_per_unit_f32,
max_rate_units_per_sec_f32, accel_units_per_sec2_f32, negative_limit_units_f32,
positive_limit_units_f32, flags]. units_tag 1=millimeters, 2=degrees when rotary
support is implemented; reject unsupported tags. flags bit 0 enables soft limits;
other bits reserved. Reject while either axis is moving/queued/homing. Require
finite positive scale/rates/acceleration and ordered limits. Apply atomically,
increment configuration_generation and acknowledge [0, generation, configured
axis mask]. Do not alter step counters when the scale changes. New boot clears
configured readiness until the main restores configuration.

MOVE_UNITS 47 request: [axis_id, absolute_target_units_f32, max_velocity_units_per_sec_f32,
acceleration_units_per_sec2_f32, configuration_generation]. JOG_UNITS 48 request:
[axis_id, signed_velocity_units_per_sec_f32, configuration_generation]; zero
requests a controlled stop. Reject stale generations, nonfinite/overflowing
values, faults, unconfigured axes and limits/rates the hardware cannot meet.
Internal target is round(target*scale), with range checks before integer cast.
Do not clamp coordinates or silently force a nonzero jog to zero on conversion.
Prefer these commands after v2 negotiation; do not convert a step target twice.

HOME_AXES 49: [axis_mask, generation], mask bits 0/1 select D/E. Implement genuine
selection; do not implement a single-axis request by silently homing both.
STOP_AXES 53: [axis_mask], with main safety events selecting mask 3. Stops remain
available before units setup and during faults when physically possible.

Motion/homing commands reply immediately on admission with [result, token,
generation]. A nonzero token on success owns the operation. Completion/cancel/
fault later echoes command/sequence plus completion flag and [result, token,
reason]. Duplicate same-sequence/same-payload requests return the original
admission/completion without re-executing; changed payload with that sequence
is an error. Zero-distance moves still produce an unambiguous accepted/completed
outcome. Define deterministic queue capacity and reject full capacity explicitly.

RELAY_WRITE 51: [channel, period_us, on_time_us], require positive period and
0 <= on <= period; zero on=off, on=period=steady on. GENERAL_OUTPUT_WRITE 52:
[channel, state_0_or_1]. Return [result, 0, generation] only after application
of commanded state. Retain legacy pulse/hold command 36 if required, documenting
its unchanged payload separately. Invalid channels must not alias local outputs.

OUTPUT_ENABLE 54: [enable_0_or_1, generation], reply [result, applied_enable,
generation]. Disable is always allowed when physically possible. Enable requires
idle/fault-free application mode, valid units/configuration for both axes, fresh
main supervision and inactive hardware E-stop. Configuration alone never enables
outputs. Boot/update/recovery force disable before accepting traffic. The main
must require an explicit operator/host enable action after update; no automatic
project continuation. Keep hardware inhibition independent of commanded GPIOs.

## Extended Feedback

EXTENDED_STATUS 46 with no payload returns 20 words. With one payload word it
sets interval_ms (0 disables, 5..2000 enables) and returns [result, interval, 0].
Periodic reports use opcode 46, no completion flag and a slave-owned sequence.
Do not mistake a 20-word snapshot for a 3-word interval acknowledgement.

| Word | Field |
| --- | --- |
| 0 | Operational API major 2 |
| 1 | Nonzero boot_id, changes on reset |
| 2 | Uptime milliseconds |
| 3 | Configuration generation |
| 4 | Configured-axis mask |
| 5 | Flags: idle bit0, updating bit1, fault bit2, homing bit3, link-lost bit4, outputs-enabled bit5 |
| 6-7 | D/E actual step counters, signed int32 |
| 8-9 | D/E actual steps/sec, signed binary32 |
| 10 | Inputs: three bits per axis (home,limit,error), D bits0-2, E bits3-5 |
| 11 | Relay commanded state: two bits/channel, 00 off,01 steady,10 pulsing,11 unknown |
| 12 | Instantaneous relay GPIO pad readback bits0-3 |
| 13 | General-output commanded mask bits0-4 |
| 14 | General-output GPIO pad readback mask bits0-4 |
| 15 | Physical-feedback valid mask: relay bits0-3, general bits4-8 |
| 16 | Physical-feedback state mask in the same numbering |
| 17-18 | Active D/E operation tokens, 0 when none |
| 19 | Fault mask: axis D bit0, axis E bit1, external E-stop bit2, watchdog bit3, protocol bit4 |

Snapshot coherently without long interrupt masking. Physical-feedback masks
must be zero if there are no sensors. PWM GPIO sampling is instantaneous and
must not be presented as relay-coil/mechanical feedback. Main converts counters
and rates using the negotiated scale, tracks age/boot ID, and marks stale state
unknown rather than off/zero. Completion messages remain the execution authority.

## Acceptance And Prototype Gaps

Required tests: fragmented/malformed frames; unknown modes/API/capabilities;
entry while busy; journal invalidation before erase; invalid size/vector/range;
CRC rejection before write; exact last-block retries; reordered blocks; full
448 KiB boundary; partial/failed flash writes; lost begin/block/commit ACKs;
power loss at each header/journal write; UART-drained reset; bad application
version/API; unit-generation races; zero/negative jog; sensor-invalid feedback;
host/main disconnect; recovery from loader with an incomplete image.

Existing pmc_slave_update_protocol/server files are early prototypes. They do
not yet implement this journal, exact-byte duplicate checking, all idempotent
entry/begin rules, application v2 units/feedback or UART reset integration.
The main updater/Telnet endpoint is not implemented. Treat this document as
the contract to implement, not proof that those paths are already operational.