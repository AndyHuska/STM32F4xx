# Client And Main-CPU Slave Firmware Streaming Contract

Version 2.0 contract, 2026-10-31. For the Visual Studio 2026 agent maintaining
the client. The companion [PMC_SLAVE_V2_API_SPEC.md](PMC_SLAVE_V2_API_SPEC.md)
defines the slave wire protocol and flash/boot requirements.

Implementation status: these commands are specified, not yet implemented on the
main CPU or in the client. The v2 slave bootloader must first be installed via
JTAG. Do not test against the current unsafe legacy update branch. No image was
flashed or client code changed while preparing this specification.

## Handoff To The Client Agent

Implement a dedicated asynchronous slave-firmware update session in the
grblHAL client, separate from normal motion/parser-ack queues. Preserve the
normal units/settings API. Do not fix this by increasing every G-code timeout
or feeding binary firmware through the G-code normalizer.

The main-CPU implementation will live in the sibling grblHAL STM32F4xx driver;
it owns boot entry, USART2 routing, safe output inhibition, peer acknowledgements
and verification. The client owns file validation, manifest CRC, user consent,
bounded block transmission, progress/cancellation, and reconnect/recovery.

Read both specification files from the shared filesystem. There is no direct
cross-IDE agent messaging API available to the VS Code assistant. The user can
give the Visual Studio agent this file as its task/handoff document. If the
client agent cannot access the sibling repository, import these two documents
into the client repo without independently changing their protocol contract.

## Preconditions

Require explicit update confirmation, connected main CPU, no active motion or
pending motion acknowledgements, and maintenance mode with physical outputs
inhibited. Suspend project execution, normal G-code/settings pushes and jogging
for the whole session. Keep the response-reader active. Main rejects BEGIN
unless its planner and remote motion are idle; it must not silently stop a
running project just because firmware data arrived.

Remote actuation remains disabled after update until application API v2,
capabilities and axis configuration are verified. Reset clears RAM-only main
settings: the client must reapply them when the main CPU itself resets.
Updating the slave must not reset the main CPU or change its MAC/IP.

First migration from a legacy application needs a separately confirmed
maintenance/bootstrap option for legacy stop/reset. Never attempt remote
bootloader replacement or mass erase. JTAG installs the v2 loader/journal layout
first; this streaming tool writes application sectors only.

## Firmware File

Accept a raw slave application BIN whose offset 0 maps to 0x08010000, with a
512-byte metadata prefix and vectors at offset 512. Not the main grblHAL BIN,
bootloader BIN, combined flash dump, textual HEX or ELF. Size: 520..458752 bytes,
multiple of four. Validate the vector rules in the slave spec before starting.
Do not silently pad/truncate; regenerate an aligned BIN if needed.

Open one stable file handle, denying writers, and retain it for the transfer.
Compute the whole-image CRC32 incrementally with a small buffer, then seek to
zero and stream. Memory must not grow with file size. Hash every byte including
metadata/gaps; do not hash filenames or HEX text. Detect early EOF/read failure.

CRC32 is ISO-HDLC, numeric check vector 123456789 -> CBF43926. Libraries may
return CRC bytes in differing order: explicitly convert to the normative uint32
and test the vector before use. Target firmware version is the exact canonical
ASCII ProductNameAndVersion expected after reboot, at most 63 characters from
[A-Z0-9._-]. Do not accept arbitrary casing/punctuation that a system-command
normalizer could change.

## Telnet Framing

Use the established main-CPU Telnet connection (currently port 23). Commands
are ASCII lines ending with LF. They are UNNUMBERED system commands: never
prepend N numbers, append G-code checksums, or inject raw binary/IAC bytes.
CRLF may be tolerated by the main but use LF to avoid empty-line extra ACKs.

Hexadecimal fields are uppercase, eight digits for uint32. Offsets/sizes are
unsigned decimal without sign. Data is exactly two hex digits per byte, without
spaces, quotes, separators or a 0x prefix. Main must provide a line buffer of at
least 1200 bytes for 512-byte hex blocks; it must reject longer lines safely.
The current firmware's parser buffer is not yet configured for this endpoint.

The response reader continuously distinguishes:
- [SLFW:...] update messages (session-correlated).
- ok / error:N command-terminal responses (still required).
- <...> ordinary status and unrelated [...] reports, which must not complete
  an update request.

Only one update RPC is outstanding. An update message without its terminal ok
does not permit sending the next command. Conversely, a bare ok without the
matching SLFW event is insufficient, especially after reconnect. Retain partial
lines across reads. Do not stop reading while awaiting a write acknowledgement.

## Session And Commands

Client creates a random nonzero uint32 session tag for correlation, formatted
as eight hex digits. It is not a password/authentication token. One session owns
the updater. Another tag is rejected as busy; ordinary clients must not steal
or interleave commands. Main reset loses its RAM session; restart with a new
session and a complete image, never assume an offset survived.

### Begin

```text
$SLFW=BEGIN,<SESSION>,<FILE_SIZE>,<IMAGE_CRC>,<TARGET_VERSION>
```

Main checks idle/maintenance preconditions, inhibits output enables, suspends
slave motion/heartbeat traffic, enters/resets the slave into its v2 loader,
requests fresh INFO, verifies mode/API/layout, and sends UPDATE_BEGIN.
Only after slave erase succeeds does main send:

```text
[SLFW:READY,<SESSION>,0,512]
ok
```

Session/manifest-identical retry must not re-erase; return current accepted
offset in READY. Different metadata with that session is rejected. Main must
not claim READY from VERSION or a reset acknowledgement alone.

### Data

```text
$SLFW=DATA,<SESSION>,<BYTE_OFFSET>,<BLOCK_CRC>,<HEX_DATA>
```

Read up to 512 bytes, normally exactly 512 except the final block. Block size
is word-aligned, equals min(512, FILE_SIZE-OFFSET), and offset is a multiple of
512. Main validates session/offset/CRC, forwards one block, and waits for the
slave's matching command/sequence/session response. It then returns:

```text
[SLFW:ACK,<SESSION>,<NEXT_OFFSET>]
ok
```

NEXT_OFFSET must equal sent offset plus actual block bytes. Advance file/progress
only after matching ACK and ok. Offset 0 is accepted/staged in the slave until
commit; ACK is not a claim that metadata was already programmed. No next block
before acceptance. Full image is never cached by the main CPU; only current/last
block and bounded state are retained. UART payload metadata plus 512 data bytes
fits the 540-byte maximum frame exactly.

### End

```text
$SLFW=END,<SESSION>
```

Main checks accepted offset equals declared file size and accumulated image
CRC matches. Slave commits after image/readback checks and valid-marker-last
journal write. Main may emit this intermediate progress message:

```text
[SLFW:COMMITTED,<SESSION>,<FILE_SIZE>,<IMAGE_CRC>]
```

This is NOT final success and has no terminal ok yet. Main requests reboot,
waits for the slave's drained ACK/reset/application startup, queries INFO,
VERSION and CAPABILITIES, and verifies operational API2/required mask and exact
TARGET_VERSION. Only then:

```text
[SLFW:DONE,<SESSION>,<ACTUAL_VERSION>,2]
ok
```

After DONE, restore/verify axis configuration generation and fresh telemetry
before enabling normal commands. Do not automatically resume the interrupted
project or energize outputs. The main uses v2 OUTPUT_ENABLE (54) only for an
explicit host/operator enable after configuration/readiness checks. Display
success only from DONE plus terminal ok.

### Status / Recovery

```text
$SLFW=STATUS,<SESSION>
[SLFW:STATE,<SESSION>,<PHASE>,<FILE_SIZE>,<NEXT_OFFSET>,<IMAGE_CRC>,<PEER_MODE>,<VERSION>,<APP_API>]
ok
```

PHASE tokens: ENTERING, ERASING, READY, WRITING, COMMITTING, REBOOTING, VERIFYING,
DONE, FAILED, ABORTED. PEER_MODE: UNKNOWN, BOOT, APP. VERSION may be UNKNOWN
until identified. STATUS does not change state or enable outputs. Use it only
between completed RPCs or after reconnect; do not assume another command can
be parsed while a blocking erase/write operation awaits its peer reply.

After a lost ACK, retain the exact pending block. Reconnect with the same tag,
query STATUS, and verify manifest matches. If next_offset equals old offset,
retry the same bytes. If it equals old offset+length, that block was accepted;
advance only after reconciling the terminal response/session state. Any other
offset/manifest/session is a recovery error, not permission to skip data.
BEGIN retry with identical manifest is also idempotent. Limit retries to three
per RPC, with STATUS reconciliation. Never invent a new tag for a block retry.

### Abort

```text
$SLFW=ABORT,<SESSION>
[SLFW:ABORTED,<SESSION>,<LAST_ACCEPTED_OFFSET>]
ok
```

Cancel at a block boundary after the outstanding RPC finishes, or reconcile a
timed-out RPC first. Main issues best-effort UPDATE_ABORT and keeps maintenance
outputs inhibited; it never runs an uncommitted image. If the connection fails,
report recovery required and preserve the log/manifest. Abort cannot restore
an erased previous image. Main retains session state for 60 seconds of client
inactivity, then fails/aborts safely. Bootloader recovery remains accessible.

## Errors And Deadlines

```text
[SLFW:ERROR,<SESSION>,<CODE>,<PHASE>,<OFFSET>]
error:<GRBL_STATUS>
```

CODE tokens: INVALID_INPUT, BUSY, SESSION, OFFSET, CRC, BOOTLOADER_UNSUPPORTED,
FLASH, TIMEOUT, IMAGE, VERSION, LINK_LOST, CANCELLED, INTERNAL. Main translates
slave numeric result codes 0-7 without losing phase/offset context. GRBL_STATUS
is the normal outer error code; update decisions come from the SLFW error.
Errors never count as ok or completed progress. A connection close/timeout
must not be shown as success just because all file bytes were transmitted.

Client defaults: BEGIN 60 seconds, DATA 10 seconds, END 60 seconds, STATUS/
ABORT 10 seconds. Main budgets boot confirmation 10 seconds, erase up to 30
seconds, block write 10 seconds, commit/reboot/verification within END budget.
Use monotonic end-to-end deadlines and cancellation tokens. Ordinary parser
ack timeout rules must not abort legitimate maintenance erase/commit waits.
Optional [SLFW:PROGRESS,...] events do not extend a deadline indefinitely.

Malformed inputs fail before erase/write. CRC error permits correction/retry
of the same current block while the session remains valid. Offset/manifest
conflicts require STATUS/recovery, not blind retry. Flash failure, unsupported
loader, mismatched final version/API and main reset are fail-closed conditions.

## Client Implementation Checklist

1. Update dialog/service with explicit target/version/file confirmation.
2. Immutable file handle, incremental CRC pass, vector/size validation.
3. Dedicated session-tagged line parser/RPC state machine, one pending command.
4. Persistent reader/demultiplexer; never treat status or old ok as update ACK.
5. 512-byte chunk and hex encoding, strict expected-next-offset checks.
6. Monotonic phase deadlines, bounded retry, STATUS reconciliation, cancellation.
7. Progress based on slave-accepted bytes, separate committed/reboot/verified UI.
8. Project/settings/jog exclusion and no automatic project restart/output enable.
9. Reconnect/recovery record containing manifest/session/last pending block.
10. Fake-main tests before real loader/application update.

Log session, phase, offset, size/CRC, send/ACK timestamps, retries and final
version/API. Raw firmware bytes need not be logged. Keep maintenance timeout
and motion-stream timeout diagnostics separate.

Acceptance tests: exact-512 multiple and short aligned final block; fragmented
network reads; stale/mismatched events/ok; competing motion; bad image/CRC;
lost READY/block ACK/COMMITTED/DONE; no-read/full-TX behavior; disconnect/main
reset; duplicate blocks; delayed erase; cancel; bad slave API/version; no full
image buffering; no UI-thread blocking; journal-recovery behavior on hardware.

The Visual Studio agent should not implement the slave binary transport in the
client: it sends this ASCII endpoint to the main CPU. Coordinate changes to
the two documents before changing IDs, field ordering, CRC, layout or replies.