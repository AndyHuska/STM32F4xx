# PMC-02-AA Bring-Up

Status: eight-axis release flashed on 2026-10-31; startup/DHCP/link checked;
physical motion and Ethernet load/reconnect behavior remain unvalidated.
This is an experimental standalone grblHAL replacement, not an image compatible
with the legacy Ethernet bootloader or binary motion protocol.

## Hardware Evidence

Original source: `C:/Users/Andy/gitlab/octo_fw/STM32F4 Projects/PMC-02-AA`.
The pin map comes from `motion/MotionPortDefs.h`, `motion/RelayPortDefs.h`,
`motion/Watchdog.h`, `motion/Watchdog.c`, `stm32f4x7_eth_bsp.c` and
`stm32f4x7_eth_bsp.h`. Clock evidence is in `system_stm32f4xx.c` and
`stm32f4xx.h`.

The old build identifies STM32F407ZG, but its
`FlashLayout_and_BootloaderConfig.md` records a physical 512 KiB flash-size
register reading. The local MCU manifest retains the nominal F407ZG identity
while enforcing 512 KiB. Confirm the populated part marking on the board.

- HSE: 12 MHz crystal, not bypass mode. PLL: M=12, N=336, P=2, Q=7.
- SYSCLK/HCLK: 168 MHz; APB1/APB2: 42/84 MHz; timer clocks: 84/168 MHz.
- PLL peripheral clock: 48 MHz. Existing F407 clock initialization is reused.
- No SD card or USB connector: features and libraries are disabled.
- RTC remains disabled because an LSE crystal has not been confirmed.

Live RCC readback confirmed PLLCFGR=0x0740540C and CFGR=0x0000940A:
SYSCLK/HCLK is 168 MHz with AHB divide-by-one, APB1=42 MHz, APB2=84 MHz.
`SystemCoreClock` also read 168,000,000. After Ethernet startup,
AHB1ENR=0x0E1000FF enabled MAC/TX/RX clocks and SYSCFG_PMC=0x00800000 selected
RMII. MDIO uses HCLK/102 (about 1.647 MHz, below its 2.5 MHz limit).
Successful MAC initialization and DHCP validate operation of the RMII clock
path, but the external 50 MHz signal has not been measured with a scope.

## Build And Shared Core

From this repository:

```powershell
pio run -e pmc_02_aa -j 8
pio run -e pmc_02_aa_6axis -j 8
pio run -e pmc_02_aa_8axis -j 8
```

VS Code tasks: `PIO: Build PMC-02-AA` and `PIO: Build PMC-02-AA Six Axis`.
They explicitly select F4 and do not upload. There is no default environment;
always select one explicitly. Artifacts are under `.pio/build/<environment>`.

### Flash And Debug

Run the `PIO: Build+Flash PMC-02-AA (STLink)` task, or its Six Axis equivalent,
to build and upload the release image. Standalone `PIO: Flash ...` tasks also
rebuild if needed. These operations overwrite the legacy firmware/bootloader;
they are never the default build task.

In Run and Debug, select `F4 PMC-02-AA: Build + Download + Debug` (or Six Axis)
and press F5. Its prelaunch task builds `pmc_02_aa_debug` (or
`pmc_02_aa_6axis_debug`) with `-Og -g3`, then Cortex-Debug downloads it and stops
at main. Standalone `PIO: Build ... Debug` tasks compile without uploading.

`F4 PMC-02-AA: Attach (No Download, No Reset)` and its Six Axis equivalent
do not build, download or reset. They halt the target for inspection and prompt
for the release/debug ELF matching the image already flashed. A stale or
different ELF gives incorrect symbols. Release builds may lack source-level
debug information. Do not run `verify_image` or `checksum_memory` on a live
initialized target: OpenOCD's RAM work area can overwrite application state.

For a manual probe inspection of an already running board, explicitly issue
`monitor resume` before GDB `detach` when restoring its original run state.
Detaching alone left this target halted; DHCP retained its lease but ARP and
ping stopped responding. Resuming restored ping (0-1 ms, no loss), including
after the temporary OpenOCD server was closed. Do not resume a deliberately
halted machine without authorization.

These configurations use the already installed Cortex-Debug extension and the
original PMC project's xPack GDB 15.2.1/OpenOCD 0.12.0 tools under its `tools`
directory, with ST-Link/SWD at 1 MHz. Those absolute tool paths must be updated
if the original project is moved. XYZ release upload and a no-download GDB
attachment using these tools passed on 2026-10-31. The VS Code debug launch UI
has not itself been exercised. The XYZ attach picker defaults to the currently
flashed `pmc_02_aa` release image.

Debug builds also passed: XYZ uses 79,696 bytes static RAM / 293,248 bytes
flash sections; six-axis uses 80,892 / 299,516 bytes.

XYZ is the default target. The six-axis variant adds three local motor channels.
The eight-axis target temporarily maps the legacy slave axes D/E to grblHAL
U/V on spare main-CPU pins. It does not communicate with the slave firmware.
The old USART2 transport on PD3-PD6 remains unallocated for a future bridge.

The initial legacy [slave bridge foundation](pmc_slave_bridge.md) now provides
packet validation, asynchronous transactions and status decoding with native
tests. It does not yet enable USART2 or replace the temporary pin mappings.

The custom VS Code task `PIO: Build PMC-02-AA Eight Axis` is under
Terminal > Run Task, not the PlatformIO toolbar. The matching
`PIO: Flash PMC-02-AA Eight Axis (STLink)` task programs this target.
Build and upload passed (82,704 bytes RAM, 276,224 bytes flash), including
OpenOCD verification and reset. A read-only `M1012 P7` query returned
`[MOSPWM7:inactive]` and `ok` afterward. No relay was switched during that check.

Both targets use the AndyHuska core/networking/miscellaneous plugin forks on
`dev/octowrapper`, aligned with the sibling H7 checkout. Submodule URL and pin
changes are not committed in this driver repository yet.

G64 path blending, velocity jog, `|AV:` and `|PQ:` reports are enabled. Jerk
acceleration is disabled. G64 remains limited to eligible XYZ G1 motion, not
arbitrary rotary/process-axis motion. `-Wdouble-promotion` is enabled; existing
core warnings remain and require profiling before high-rate motion is accepted.

F4 driver compatibility repairs use the core's delay callback instead of the
newer `task_execute()` API, adapt timer capability selection/resolution,
initialize `hal.settings_changed`, and advertise `hal.driver_cap.amass_level`
as required by the pinned core. The shared core itself was not edited here.

## Motor Pins

| Channel | Step | Direction | Enable | Home | Limit | Error |
| --- | --- | --- | --- | --- | --- | --- |
| X | PA6 | PD8 | PD9 | PF0 | PF1 | PF2 |
| Y | PB6 | PD10 | PD11 | PF3 | PF4 | PF5 |
| Z | PA0 | PD12 | PD13 | PF6 | PF7 | PF8 |
| A | PA9 | PD14 | PD15 | PF9 | PF10 | PF11 |
| B | PB10 | PE0 | PE1 | PF12 | PF13 | PF14 |
| C | PC6 | PE2 | PE3 | PF15 | PG1, auxiliary only | PG2 |
| U (temporary D) | PC7 | PE6 | PE7 | PE11 | PC2 | PE12 |
| V (temporary E) | PC8 | PE9 | PE10 | PE14 | PE8 | PE13 |

The U/V assignments are unused MCU resources, not verified PCB connector
wiring. They apply only when building eight axes. Their error inputs are
readable as AUXINPUT8/9, not assigned automatically as motor faults.

grblHAL uses GPIO bit-band step writes and its shared step timer, not the legacy
per-axis timer/DMA engine. Driver polarity, steps/unit, travel, acceleration,
and pulse timing must be configured and measured; old machine settings are not
automatically migrated.

**C has no automatic hard-limit interrupt.** X limit PF1 and C limit PG1 share
STM32 EXTI line 1, which can select only one GPIO port. X keeps its hard-limit
interrupt. PG1 is AUXINPUT7, readable but not an automatic C stop. Do not rely
on it for protection until a validated polling/hardware solution is added.
C home PF15 retains its separate home mapping.

Fault inputs PF2/PF5/PF8/PF11/PF14/PG2 are AUXINPUT0..5. They are readable but
not automatically assigned as motor alarms. PD1 is AUXINPUT6. Raw input mapping
does not constitute an implemented machine safety policy.

## General-Purpose Outputs

The five digital outputs preserve the original `Dashboard.c`
`writeGeneralOutputBit()` numbering:

| Digital port | GPIO |
| --- | --- |
| P0 | PC14 |
| P1 | PC15 |
| P2 | PD7 |
| P3 | PE4 |
| P4 | PE15 |

`M64 Pn` sets a digital output; `M65 Pn` clears it. M62/M63 are the
motion-synchronized equivalents. `N1427 M64 P1` therefore addresses PC15,
not PWM relay channel 1. Before this mapping, digital output capability was
absent and M64 was rejected with error 20. The flashed eight-axis firmware
now reports `[AUX IO:10,5,0,8]` over `$I+`, confirming five digital and eight
PWM/analog outputs. Only read-only queries were used for this verification.

PC14/PC15 use low GPIO speed. Their fixed legacy roles take priority over
temporary spare assignments: U direction was moved from PE4 to PE6, and V
error input from PE15 to PE13. These maps are inherited by all PMC targets.
The eight-axis image was built, programmed, verified and reset after the change
(82,844 bytes static RAM, 276,396 bytes flash). Output-buffer enable remains
`PMC_OUTPUTS_ENABLE=0`.

## Output Gate And GPIOs

PD0 is the legacy active-low, open-drain output-buffer enable. Startup sets it
high before making it an open-drain output, leaving buffers disabled.
`PMC_OUTPUTS_ENABLE=0` is intentional in both targets.

**Do not change that flag to 1 before verifying polarities and external E-stop
wiring with drive power disconnected.** A value of 1 pulls PD0 low at board
initialization; it does not implement the old heartbeat watchdog or a new
state-dependent safety interlock. The default firmware can communicate and
plan without enabling physical outputs.

PD1 remains an input, matching the original external E-stop/second gate setup.
Confirm its actual electrical polarity and hardware-gate wiring. It is not
assigned to grblHAL Reset/E-stop. Control buttons, probe, spindle and coolant
are disabled rather than assigned guessed pins.

| PWM channel | Assignment | GPIO | Timer/channel |
| --- | --- | --- | --- |
| P0 | Original relay 1 | PE5 | TIM9 CH1 |
| P1 | Original relay 2 | PB8 | TIM10 CH1 |
| P2 | Original relay 3 | PB9 | TIM11 CH1 |
| P3 | Original relay 4 | PB15 | TIM12 CH2 |
| P4 | Temporary main-CPU relay 5 | PA8 | TIM1 CH1 |
| P5 | Temporary main-CPU relay 6 | PA3 | TIM2 CH4 |
| P6 | Temporary main-CPU relay 7 | PB4 | TIM3 CH1 |
| P7 | Temporary main-CPU relay 8 | PB7 | TIM4 CH2 |

The complete H7 plugin command/reporting implementation is enabled for PMC,
using eight independent PWM timers. The original four pins/timers come from
the old firmware; P4-P7 are temporary spare MCU pins, not slave outputs.
`M1010 Pn F<hz> D<percent>` sets frequency/duty. `M1011 Pn Q<period_us>
R<on_us>` uses microseconds: Q1000/R1000 is continuously on, R0 is off.
M1010 or M1011 without set values reports a channel; M1012 reports all, or
one channel with P. Relay status keeps the H7 two-bit off/on/pulsing encoding.
The historical H743_MOSFET_PWM_ENABLE flag/API names are retained for the
shared core's plugin initialization and status hooks.

Relays are PWM/analog I/O ports, separate from the five general-purpose
digital M64/M65 ports listed above. PWM ports
are claimed lazily when commanded; inactive reports before use are expected.
Legacy dual-relay pulse-and-hold commands and actual slave forwarding remain
separate future work. The board map prohibits generic I2C/SD wiring; PB8/PB9
are relay outputs. The plugin port is currently uncommitted in F4's plugins
fork checkout; H7's plugin checkout was not edited.

USART3 PC10/PC11 is an optional 115200-baud console without mapped peripheral
conflicts. Access to those pins on the physical board/header is unconfirmed.
PC11 now has an idle-high pull-up via `SERIAL_RX_PULL_MODE`; leaving it floating
caused noise-triggered realtime reports to block in the UART interrupt handler.
After the pull-up, the probe found normal Thread-mode protocol execution and
no pending UART RX or fault flags. There is no USB console.

## Ethernet

Native STM32 MAC, RMII, Micrel KSZ8001/KSZ8721-family PHY, MDIO address 1:

| Signal | GPIO |
| --- | --- |
| REF_CLK, external 50 MHz | PA1 |
| MDIO | PA2 |
| CRS_DV | PA7 |
| MDC | PC1 |
| RXD0/RXD1 | PC4/PC5 |
| TX_EN | PG11 |
| TXD0/TXD1 | PG13/PG14 |

The old PHY interrupt is PB14. This port polls link state every 100 ms and does
not consume that EXTI line. No PHY reset GPIO or PA8 MCO clock is assumed;
the board must supply its existing external RMII reference clock.

DHCP, Telnet and WebSocket are enabled. HTTP/WebUI, FTP, mDNS and SSDP are
disabled. Persisted grblHAL network settings can override build defaults.
The old binary TCP/UDP server is not carried over. The environment now sets
`BOARD_ETH_MAC=0x40,0xD8,0x55,0x19,0xC0,0x00` for this board. It is a six-byte
override of the UID-generated default, with no automatic LAA-bit modification.
Probe readback confirmed 40:D8:55:19:C0:00 in the HAL buffer and MAC registers
(MACA0HR=0x800000C0, MACA0LR=0x1955D840). All PMC variants inherit this value;
distinct physical boards must not share it on the same network.

DHCP acquired 192.168.1.218 on `st0` with this MAC on 2026-10-31. That is a
lease, not a firmware-defined static IP. The MAC reported 100 Mbps full duplex.

STM32CubeF4 ~1.28.1 (resolved to 1.28.3 in validation) supplies the newer HAL
Ethernet API required by the existing adapter. The older framework remains
the default for unrelated environments. lwIP, RMII and PHY libraries are
symlinked to repository sources with manifests to avoid stale cached copies.

The original ST example code names DP83848, but direct MDIO reads found PHY ID
0x00221619 at address 1, and no PHY at address 0. This matches the Micrel
KSZ8001/KSZ8721 family, not DP83848. PMC selects `ETH_PHY_KSZ8721=1` and the
dedicated `Drivers/BSP/Components/ksz8721` component. It checks identity,
enables autonegotiation, and resolves 10/100 speed/duplex through standard
Clause 22 status and advertised/partner abilities. The adapter keeps its
DP83848 option for other boards; the directory name is retained for compatibility.

Ethernet changes also select GPIOG transmit pins while preserving GPIOB
defaults, retain the MAC buffer after initialization, propagate HAL/PHY
initialization and transmit failures, and enable custom RX pbufs and network
status callbacks with a 12 KiB lwIP heap.

## Flash And RAM

All PMC environments explicitly set `FLASH_ENABLE=0` and `EEPROM_ENABLE=0`.
There is no EEPROM/SD settings backend or flash emulation. The core retains
its RAM settings buffer so networking/plugins can store live configuration.
Settings and parameters do not survive power loss or reset; the client must
reapply them. The configured MAC and other compile-time defaults remain.

The RAM-only eight-axis build passed and was programmed, verified and reset
on 2026-10-31 (82,792 bytes static RAM, 275,264 bytes flash sections). The ELF
contains no `memcpy_to_flash`/`memcpy_from_flash` functions. Probe readback
confirmed physical_nvs.type=0 (`NVS_None`), a 2 KiB RAM buffer, driver startup
0xFF, clear fault registers and DHCP address 192.168.1.218. The probe was
resumed/detached and released. This removes settings flash erase/program
latency; the separate G64/status-stream stall is still under investigation.

The reused `STM32F407VETX_FLASH.ld` enforces 512 KiB independently of the MCU
package name. Vectors are at 0x08000000, an unused former NVS reservation
remains at 0x08004000, and code
starts at 0x08008000. Flash ends at 0x08080000. Uploading this standalone image
replaces the legacy bootloader/vector layout. No legacy-bootloader-compatible
target is provided. The XYZ release image has now been uploaded.

Main SRAM is 128 KiB at 0x20000000; CCM is 64 KiB at 0x10000000. This linker
puts the general heap and stack in CCM, while data/BSS and lwIP allocations
remain in main SRAM. Do not pass CCM stack/malloc buffers directly to DMA.
RX/TX descriptors, RX pool, MAC array and lwIP heap were checked in main SRAM.
Initialized CCM data copying is not added here.

Recorded release build results on 2026-10-31:

| Environment | Static RAM | Flash sections |
| --- | --- | --- |
| pmc_02_aa | 79,636 bytes | 261,824 bytes |
| pmc_02_aa_6axis | 80,840 bytes | 267,392 bytes |

These are section sizes, not peak stack/heap use or binary size including gaps.
Both environments built successfully, including simultaneous builds.
XYZ PHY initialization, link-up and DHCP have been checked on hardware.
Sustained traffic, cable reconnect, overrides and physical motion remain pending.

## First Upload Findings

The initial `LIBUSB_ERROR_ACCESS` came from the old H7 OpenOCD/GDB session
holding the only ST-Link. That session was stopped and the probe reconnected;
the F4 upload then programmed, verified and reset successfully. Do not run two
debuggers/programmers against the same probe simultaneously.

The first uploaded image faulted on a null indirect call in `settings_init()`.
The older core calls `hal.settings_changed`; the newer F4 driver only assigned
`grbl.on_settings_changed`. Restoring the HAL callback assignment fixed startup.
The corrected XYZ release image was programmed and verified, then reset. A
no-download GDB attachment found the protocol loop in Thread mode, CFSR/HFSR
both zero, and the HAL callback non-null. No live RAM checksum verification was
used. Temporary debug servers were closed afterward to release the probe.

Six-axis and debug build results above predate this one-line callback fix;
their build tasks rebuild changed sources automatically. Attach never builds,
so its ELF must match the actual firmware already programmed. Outputs remain
disabled by `PMC_OUTPUTS_ENABLE=0`.

The subsequent missing-IP diagnosis found driver startup flags 0xF7: only
AMASS capability was failing. The core requires an advertised AMASS level;
without it, driver setup and all Ethernet clock/PHY work were skipped. Setting
`hal.driver_cap.amass_level = MAX_AMASS_LEVEL` changed startup flags to 0xFF
and IOInitDone to 1. Correcting PHY selection then created the interface and
obtained DHCP automatically. Older six-axis/debug sizes above also predate
these changes; rebuild those targets before using their new images.

## Hardware Acceptance

1. Confirm MCU marking/flash density and HSE/168 MHz clocks after reset.
2. PHY ID, link-up, DHCP and MAC speed/duplex have been checked. Still measure
   PA1 reference frequency and test static IP, cable reconnect and load.
3. Measure PD0/PD1 levels and relay/motor polarity with drive power off.
4. Validate pulse widths, independent enables, direction, home/limits; assign
   fault/E-stop policy before enabling real motion.
5. Measure segment-generation/step timing under G64/network load and SRAM/CCM
   high-water marks. Default pulse-latency compensation needs F407 calibration.
6. Separately implement/validate required C-limit polling, heartbeat watchdog,
   relay PWM, slave control or legacy protocol compatibility.

Build/startup success is not evidence of physical Ethernet operation, machine
safety or motion timing. The eight-axis release is now flashed; nothing was committed
or pushed in this bring-up.