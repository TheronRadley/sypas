/* Small, freestanding ACPI RSDP validator used by the UEFI loader. */
#ifndef SYPAS_LOADER_ACPI_H
#define SYPAS_LOADER_ACPI_H

#include <stdint.h>

typedef enum {
    SYPAS_ACPI_RSDP_OK = 0,
    SYPAS_ACPI_RSDP_ERR_NULL,
    SYPAS_ACPI_RSDP_ERR_SIGNATURE,
    SYPAS_ACPI_RSDP_ERR_CHECKSUM,
    SYPAS_ACPI_RSDP_ERR_REVISION,
    SYPAS_ACPI_RSDP_ERR_LENGTH,
    SYPAS_ACPI_RSDP_ERR_EXTENDED_CHECKSUM,
} sypas_acpi_rsdp_status_t;

/* Validate an RSDP found through an EFI configuration table.  RSDP v1 is
 * 20 bytes; ACPI 2.0+ is validated through its advertised (bounded) length,
 * including the extended checksum. */
sypas_acpi_rsdp_status_t sypas_acpi_validate_rsdp(const void *rsdp);
const char *sypas_acpi_rsdp_status_str(sypas_acpi_rsdp_status_t status);

#endif /* SYPAS_LOADER_ACPI_H */
