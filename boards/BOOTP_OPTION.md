# F4 Native Ethernet BOOTP Option

Added 2026-11-05. Packet tests and firmware builds pass; live BOOTP server
acquisition/reconnect has not yet been tested. Nothing was flashed for this work.

## Selection

Native F4 Ethernet supports `BOOTP_ENABLE=1` with acquisition mode 3:

| NETWORK_IPMODE / $301 | Meaning |
| --- | --- |
| 0 | Static |
| 1 | DHCP |
| 2 | Existing AutoIP selection |
| 3 | BOOTP, available when BOOTP_ENABLE is enabled |

Existing values are unchanged. This change does not alter AutoIP behavior.
BOOTP is not implemented for the Wiznet or H7 Ethernet path. Shared mode enum
and compile-time validation definitions are kept consistent in both core
checkouts; H7 does not enable or advertise the new acquisition option.

All PMC builds include BOOTP support. Standalone targets keep DHCP as their
default; legacy release/debug targets select BOOTP by default. The legacy
environments remove inherited NETWORK_IPMODE=1 before adding mode 3.

Build settings:

```ini
  -D BOOTP_ENABLE=1
  -D NETWORK_IPMODE=3
  -D ETHERNET_DHCP_SERVER_ENABLE=0
```

`$301` exposes Static,DHCP,AutoIP,BOOTP when the feature is compiled. Settings
still require reboot to apply; PMC uses RAM-only settings, so a runtime mode
change does not survive reset. Select the acquisition default in the build for
these targets. Client enums/UI must preserve 0/1/2 and accept BOOTP=3, and must
not blindly push DHCP over the legacy build's desired BOOTP configuration.

## Behavior

The application independently reacquires its address, matching the original
firmware. It does not inherit bootloader RAM, lwIP pointers or a previous lease.
BOOTP uses UDP client port 68 to server port 67 and requests broadcast replies.
The factory MAC identifies the board. XID is unique per acquisition attempt and
retained across retries; replies must match XID, Ethernet type/length and MAC.

Acquisition starts after link-up. Retry intervals are 1,2,4,8,16 seconds and
remain bounded at 16 seconds. Allocation/bind failures are retried. Successful
reply parsing installs IP/mask/gateway on the existing netif; the ordinary
status callback starts configured Telnet/WebSocket services. BOOTP mode never
starts a DHCP client and never falls back to DHCP silently. If no server replies,
the board remains unconfigured and continues retrying.

Link-down closes the BOOTP PCB, clears the acquired address/state, and link-up
begins a new transaction. Accepted configuration is retained until link loss
or application reset; BOOTP has no DHCP lease renewal state. Pending link-state
events use the existing grblHAL network reporting path.

## Legacy Identity And Reply Options

The request is a zero-initialized 304-byte BOOTP packet. Generic mode includes
the RFC vendor cookie and no DHCP message-type option. Legacy PMC builds use
the original format: zero cookie, loader version in the file field, package
application version in the 64-byte vendor field. Loader identity is read only
when its bounded factory metadata field has the expected PMC prefix. Version
fields are informational to the acquisition service; no TFTP request/run/update
is sent by this client. The firmware update tool and immutable loaders remain
unchanged.

Reply handling copies chained pbuf data into a bounded 576-byte buffer and
rejects packets outside it. It accepts legacy BOOTP without RFC options and
parses standard subnet-mask (1) and router (3) TLVs when the RFC cookie is
present. Truncated/duplicate required options, DHCP message-type (53), option
overload (52), invalid IP and noncontiguous masks are rejected.

If no mask is provided, use the offered address's classful mask, not the old
firmware's zero-mask-based class selection. If no gateway is provided, leave
it zero rather than invent a router. Ensure the BOOTP server supplies correct
mask/gateway for non-classful or routed installations. Only an address/mask
consistent with ordinary unicast host use is accepted; /31 and /32 are allowed.

## DHCP Server Policy

The old native Ethernet static-IP callback started a DHCP server as a separate
side effect. `ETHERNET_DHCP_SERVER_ENABLE` now gates that call; its default
preserves behavior for other F4 boards, and PMC explicitly sets it to 0.
Neither static nor BOOTP PMC operation advertises the board as a DHCP server.

## Validation

Native test:

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
gcc -std=c11 -Wall -Wextra -Werror tests/bootp_packet_test.c boards/bootp_packet.c -o "$env:TEMP/bootp_packet_test.exe"
& "$env:TEMP/bootp_packet_test.exe"
```

Tests passed for legacy identity and standard requests, network byte order,
legacy/classful and RFC replies, mask/router options, wrong XID/MAC/type/op,
short/malformed packets, DHCP/overload rejection and invalid subnet/address.
Legacy release/debug, standalone DHCP eight-axis and H7 Waveshare builds pass.
The current lwIP input path accepts client UDP port 68 with a zero local IP.

Hardware gates: capture request/reply on the real BOOTP service, validate
legacy identity expectations, IP/mask/gateway, ping/Telnet, no DHCP discover,
no-server retry/backoff, cable reconnect, actual loader RUN reacquisition and
multiple devices with their own factory MACs. Compiler/packet tests alone do
not establish live service compatibility. Do not flash a legacy image over
bootloader sectors to run these checks.