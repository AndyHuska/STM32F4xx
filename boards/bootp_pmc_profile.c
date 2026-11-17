#include "bootp_pmc_profile.h"

#include <string.h>

bootp_request_profile_t bootp_pmc_profile(const char *loader_version, const char *application_version)
{
    return (bootp_request_profile_t){
        .file_identity = loader_version && !strncmp(loader_version, "PMC", 3)
            ? loader_version : "PMC-02-AA_GRBLHAL",
        .vendor_identity = application_version ? application_version : "PMC-02-AA_GRBLHAL",
        .legacy_vendor_identity = true
    };
}