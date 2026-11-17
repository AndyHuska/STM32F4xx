# F4 Legacy Bootloader Application Build

Status: release/debug/standalone builds and binary layout checks passed on
2026-11-04. Actual legacy-loader RUN handoff and direct-entry debugging have not
been exercised on hardware. No flash/reset operation was performed for this work.
H7 files and its build/startup policy were not changed.

## Build Choices

| Environment | Intended use |
| --- | --- |
| pmc_02_aa_8axis | Existing standalone image; cold boot without a legacy loader |
| pmc_02_aa_8axis_debug | Standalone debug image |
| pmc_02_aa_8axis_legacy | Application for the immutable legacy loader/package tool |
| pmc_02_aa_8axis_legacy_debug | Same application layout with debug symbols and bench MAC fallback |

Tasks: `PIO: Build PMC-02-AA Eight Axis Legacy Bootloader` and
`PIO: Build PMC-02-AA Eight Axis Legacy Debug`. These are build-only.
No automatic legacy upload/bootloader restoration task was added.

A single ordinary STM32 reset vector cannot live at both 0x08000000 and
0x08010200. The legacy-linked application can nevertheless be loaded and
started directly with a probe, without an installed/running loader. This is
debugger-assisted entry, not standalone power-on boot. Use the existing
standalone environment for cold boot without the loader. Do not add a reset
stub at address zero to the field package: it would overwrite the fixed loader.

## Image Contract

`STM32F407_PMC_LEGACY_FLASH.ld` generates:

| Region | Address / rule |
| --- | --- |
| Bootloader, untouched by application ELF/BIN | Below 0x08010000 |
| Factory MAC, read-only to application | 0x0800FF00, six bytes |
| Loader version, read-only | 0x0800FF08, 64 bytes |
| Application package version | 0x08010000, exactly 64 bytes, zero-padded |
| Application vectors | 0x08010200 |
| Flash end, exclusive | 0x08080000 |
| Initial MSP | 0x2001FFF8, main SRAM |

The immutable loader rejects CCM as the initial stack and its mask also rejects
0x20020000. The linker asserts the accepted stack mask, vector address, version
size, flash region and minimum heap/stack reservation. Heap and stack use main
SRAM on this variant; data/BSS and Ethernet DMA buffers remain in main SRAM.
Initialized CCM sections are copied by the custom reset entry when present.

BIN offset zero is 0x08010000, not the vector table. Version-to-vector gaps are
included in the BIN. Never give the old package tool the standalone BIN, ELF,
or a combined loader/application dump. No application section is emitted at the
factory MAC/loader-version addresses.

The default package version is `PMC-02-AA_GRBLHAL_2.0.0` in
`Src/pmc_package_version.c`. `PMC_PACKAGE_VERSION` can override that string.
Set the slave application's legacy ProductNameAndVersion field to the exact
same 64 zero-padded bytes. The old packager compares the entire field, and the
master loader refuses to run on a slave version mismatch. Package version and
operational slave API v2 are separate contracts.

## Early Handoff Cleanup

The legacy loader jumps with peripherals live and does not provide a complete
hardware reset. `Src/pmc_legacy_entry.S` supplies a strong Reset_Handler for the
legacy environments; the framework's vector table is retained explicitly.
Entry masks IRQs, selects MSP, and invokes `pmc_legacy_early_init()` BEFORE
copying data or clearing BSS. This ordering was checked in the generated code.

The register-only early function requires no initialized RAM/HAL tick. It
inhibits PD0 output buffers, stops/resets Ethernet and DMA, disables/clears
NVIC and SysTick/PendSV state, switches to ready HSI before disabling PLLs,
normalizes bus clocks/resets, and relocates VTOR. Clock-ready loops are bounded;
failure stays output-inhibited with IRQs masked rather than using HAL timeouts.
Runtime initialization then copies data/CCM, clears BSS, runs SystemInit and
constructors, and starts main. HAL configures the normal 12 MHz HSE / 168 MHz
clock afterward. Standalone startup remains unchanged. The unrelated ROM-loader
RAM sentinel is not interpreted in the legacy environment.

This must still be scoped/tested at the real handoff under network load. A
compile/disassembly check cannot prove the peripheral/output timing or physical
safe state. The original bootloader itself is not modified.

## MAC And Network

Production legacy builds use the factory MAC only when the legacy loader-version
field has its expected PMC prefix and a bounded terminator, and the six MAC
bytes are neither all-zero/all-FF nor multicast. Invalid/missing factory metadata
fails network initialization, rather than deploying one bench MAC to every unit.

The legacy DEBUG environment sets `PMC_LEGACY_MAC_FALLBACK=1`, permitting the
configured BOARD_ETH_MAC bench value when the loader metadata is absent. This
supports direct app debugging on a standalone bench target. Do not package the
debug fallback image as a production release.

The application initializes its own current lwIP/MAC stack; no old lwIP heap,
netif, lease or pointers are reused. The loader's BOOTP-acquired IP is not a
defined handoff variable and is not inherited. Legacy release/debug now select
the [BOOTP option](BOOTP_OPTION.md) to reacquire configuration independently,
matching the original application's behavior; standalone still defaults to
DHCP. Live BOOTP service and loader RUN reacquisition remain untested. Verify
same-MAC addressing, mask/gateway, client rediscovery and link/ARP across RUN.

## Debug Without Running The Loader

Run and Debug entries:

- `F4 PMC-02-AA Legacy: Debug (Direct App Entry)` builds the legacy debug ELF,
  resets/halts the target, loads only application sections, then sets SP/PC from
  vectors at 0x08010200/0x08010204 and runs to main. It does not need TFTP RUN
  or a matching slave just to debug application startup. Restart uses the same
  direct-entry sequence without downloading again.
- `F4 PMC-02-AA Legacy: Attach (No Download, No Reset)` chooses a matching
  legacy release/debug ELF and halts for inspection without building/uploading.
  Do not attach with a standalone ELF to a legacy-linked image or vice versa.

Direct-entry debugging bypasses the loader's version/RUN behavior, so it is not
a substitute for testing the loader handoff. A cold reset without the debugger
still follows whatever is at address zero. Restoring/testing a legacy loader on
a bench board previously flashed standalone requires a separately authorized,
known bootloader image; do not assume it survived earlier standalone flashing.
No loader image is bundled or written by these configurations.

Manual inspections must restore the prior running state when authorized
(`monitor resume` before detach), and never use live RAM checksum helpers.

## Binary Validation

```powershell
.\tools\Test-PmcLegacyImage.ps1 -Environment pmc_02_aa_8axis_legacy
.\tools\Test-PmcLegacyImage.ps1 -Environment pmc_02_aa_8axis_legacy_debug
.\tools\Test-PmcLegacyImage.ps1 -Environment pmc_02_aa_8axis_legacy -SlaveImage <slave-application.bin>
```

The last form also verifies exact master/slave version-field equality. If local
execution policy blocks the script, use an approved process-only local-script
policy; do not change machine policy or weaken managed restrictions. The script
uses only local build artifacts and performs no target access.

Release, debug and standalone eight-axis builds passed. Release/debug binaries
passed size/version/Thumb-entry/stack-mask checks, and disassembly confirmed
cleanup before the data/BSS loops. Loadable sections are application-only.
Physical loader boot, TFTP packaging/RUN, direct-debug entry, clock/MAC/DMA
behavior and slave supervision remain hardware acceptance gates.

## Deployment Policy

Keep the existing package tool and fixed MBOOTLOADER/SBOOTLOADER update flow:
master and slave application BINs, matching metadata, the old package CRCs,
BOOTP/TFTP and custom TFTP_QUIT/RUN opcode 6. Do not update bootloaders, reserve
a new journal in their sectors, or implement a main-application update bridge.
The old loader retains its existing block-1-last validity limitations; this
application build does not retroactively strengthen the immutable loader.

The operational v2 slave bridge still needs its UART implementation and
supervision/configuration integration. It is not enabled by these build flags.
The new build is an application-layout/startup implementation for bench
validation, not yet a qualified field release.