/* Freestanding ACPI RSDP validation.  Kept independent of UEFI for tests. */
#include "acpi.h"

#define RSDP_V1_LENGTH 20U
#define RSDP_V2_MIN_LENGTH 36U
#define RSDP_MAX_LENGTH 4096U

typedef struct __attribute__((packed)) {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} acpi_rsdp_t;

_Static_assert(sizeof(acpi_rsdp_t) == RSDP_V2_MIN_LENGTH, "RSDP layout");

static int equal_signature(const char *signature)
{
    static const char expected[] = "RSD PTR ";
    for (int i = 0; i < 8; i++)
        if (signature[i] != expected[i])
            return 0;
    return 1;
}

static int checksum_ok(const uint8_t *data, uint32_t length)
{
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++)
        sum = (uint8_t)(sum + data[i]);
    return sum == 0;
}

sypas_acpi_rsdp_status_t sypas_acpi_validate_rsdp(const void *rsdp)
{
    if (!rsdp)
        return SYPAS_ACPI_RSDP_ERR_NULL;

    const acpi_rsdp_t *r = (const acpi_rsdp_t *)rsdp;
    if (!equal_signature(r->signature))
        return SYPAS_ACPI_RSDP_ERR_SIGNATURE;
    if (!checksum_ok((const uint8_t *)r, RSDP_V1_LENGTH))
        return SYPAS_ACPI_RSDP_ERR_CHECKSUM;
    if (r->revision == 0)
        return SYPAS_ACPI_RSDP_OK;
    if (r->revision < 2)
        return SYPAS_ACPI_RSDP_ERR_REVISION;
    if (r->length < RSDP_V2_MIN_LENGTH || r->length > RSDP_MAX_LENGTH)
        return SYPAS_ACPI_RSDP_ERR_LENGTH;
    if (!checksum_ok((const uint8_t *)r, r->length))
        return SYPAS_ACPI_RSDP_ERR_EXTENDED_CHECKSUM;
    return SYPAS_ACPI_RSDP_OK;
}

const char *sypas_acpi_rsdp_status_str(sypas_acpi_rsdp_status_t status)
{
    switch (status) {
    case SYPAS_ACPI_RSDP_OK:                    return "ok";
    case SYPAS_ACPI_RSDP_ERR_NULL:              return "null";
    case SYPAS_ACPI_RSDP_ERR_SIGNATURE:         return "bad signature";
    case SYPAS_ACPI_RSDP_ERR_CHECKSUM:          return "bad checksum";
    case SYPAS_ACPI_RSDP_ERR_REVISION:          return "unsupported revision";
    case SYPAS_ACPI_RSDP_ERR_LENGTH:            return "bad extended length";
    case SYPAS_ACPI_RSDP_ERR_EXTENDED_CHECKSUM: return "bad extended checksum";
    }
    return "unknown";
}
