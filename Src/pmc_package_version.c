#if defined(BOARD_PMC_02_AA) && defined(PMC_LEGACY_BOOTLOADER)

#ifndef PMC_PACKAGE_VERSION
#define PMC_PACKAGE_VERSION "PMC-02-AA_GRBLHAL_2.0.0"
#endif

_Static_assert(sizeof(PMC_PACKAGE_VERSION) <= 64, "PMC package version exceeds the legacy 64-byte field");
__attribute__((section(".fwversion"), used))
const char pmc_package_version[64] = PMC_PACKAGE_VERSION;

#endif