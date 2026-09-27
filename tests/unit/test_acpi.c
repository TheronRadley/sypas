/* Host tests for UEFI-loader ACPI RSDP validation. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "acpi.h"

static int failures;

#define CHECK(condition, name)                                            \
    do {                                                                   \
        if (condition)                                                     \
            printf("PASS %s\n", name);                                   \
        else {                                                             \
            printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__);      \
            failures++;                                                    \
        }                                                                  \
    } while (0)

typedef struct __attribute__((packed)) {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} rsdp_t;

static void fix_checksum(uint8_t *data, size_t length, size_t checksum_byte)
{
    uint8_t sum = 0;
    data[checksum_byte] = 0;
    for (size_t i = 0; i < length; i++)
        sum = (uint8_t)(sum + data[i]);
    data[checksum_byte] = (uint8_t)(0 - sum);
}

static void valid_v2(rsdp_t *r)
{
    memset(r, 0, sizeof(*r));
    memcpy(r->signature, "RSD PTR ", 8);
    memcpy(r->oem_id, "SYPAS ", 6);
    r->revision = 2;
    r->length = sizeof(*r);
    r->rsdt_address = 0x1000;
    r->xsdt_address = 0x2000;
    fix_checksum((uint8_t *)r, 20, 8);
    fix_checksum((uint8_t *)r, sizeof(*r), 32);
}

int main(void)
{
    rsdp_t r;

    valid_v2(&r);
    CHECK(sypas_acpi_validate_rsdp(&r) == SYPAS_ACPI_RSDP_OK,
          "valid ACPI 2.0 RSDP accepted");

    valid_v2(&r);
    r.signature[0] = 'X';
    CHECK(sypas_acpi_validate_rsdp(&r) == SYPAS_ACPI_RSDP_ERR_SIGNATURE,
          "bad RSDP signature rejected");

    valid_v2(&r);
    r.oem_id[0] ^= 1;
    CHECK(sypas_acpi_validate_rsdp(&r) == SYPAS_ACPI_RSDP_ERR_CHECKSUM,
          "bad base checksum rejected");

    valid_v2(&r);
    r.revision = 1;
    fix_checksum((uint8_t *)&r, 20, 8);
    CHECK(sypas_acpi_validate_rsdp(&r) == SYPAS_ACPI_RSDP_ERR_REVISION,
          "unsupported RSDP revision rejected");

    valid_v2(&r);
    r.length = 35;
    fix_checksum((uint8_t *)&r, 20, 8);
    CHECK(sypas_acpi_validate_rsdp(&r) == SYPAS_ACPI_RSDP_ERR_LENGTH,
          "short extended RSDP rejected");

    valid_v2(&r);
    r.extended_checksum ^= 1;
    CHECK(sypas_acpi_validate_rsdp(&r) == SYPAS_ACPI_RSDP_ERR_EXTENDED_CHECKSUM,
          "bad extended checksum rejected");

    valid_v2(&r);
    r.revision = 0;
    r.length = 0; /* ignored by ACPI 1.0 validation */
    fix_checksum((uint8_t *)&r, 20, 8);
    CHECK(sypas_acpi_validate_rsdp(&r) == SYPAS_ACPI_RSDP_OK,
          "valid ACPI 1.0 RSDP accepted");

    CHECK(sypas_acpi_validate_rsdp(0) == SYPAS_ACPI_RSDP_ERR_NULL,
          "null RSDP rejected");

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
