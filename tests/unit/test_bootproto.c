/*
 * SYPAS host-side unit tests — boot protocol contract
 * (bootloader/protocols/sypas_bootproto.h).
 *
 * Most of the enforcement is compile-time: including the header pulls in
 * the _Static_assert layout checks, so merely building this test on the
 * host proves the ABI layout is what both sides believe it is.  The
 * runtime part exercises the SYPAS_BI_HAS() minor-version probing rule.
 */

#include <stdio.h>
#include <string.h>

#include "../../bootloader/protocols/sypas_bootproto.h"

static int failures;

#define CHECK(cond, name)                                                \
    do {                                                                 \
        if (cond) {                                                      \
            printf("PASS %s\n", name);                                   \
        } else {                                                         \
            printf("FAIL %s (%s:%d)\n", name, __FILE__, __LINE__);       \
            failures++;                                                  \
        }                                                                \
    } while (0)

int main(void)
{
    sypas_bootinfo_t bi;
    memset(&bi, 0, sizeof(bi));
    bi.magic         = SYPAS_BOOT_MAGIC;
    bi.version_major = SYPAS_BOOT_VERSION_MAJOR;
    bi.version_minor = SYPAS_BOOT_VERSION_MINOR;

    /* Full v1.0 structure: every v1.0 field is present. */
    bi.size = sizeof(bi);
    CHECK(SYPAS_BI_HAS(&bi, memmap),     "full size: memmap present");
    CHECK(SYPAS_BI_HAS(&bi, stack_size), "full size: stack_size present");
    CHECK(SYPAS_BI_HAS(&bi, cmdline),    "full size: cmdline present");
    CHECK(bi.size >= SYPAS_BOOTINFO_V1_0_SIZE, "full size >= v1.0 prefix");

    /* A hypothetical older/shorter producer: fields beyond `size` must
     * probe as absent, fields inside it as present. */
    bi.size = (uint32_t)offsetof(sypas_bootinfo_t, stack_base);
    CHECK(SYPAS_BI_HAS(&bi, kernel_size),  "cut: kernel_size still present");
    CHECK(!SYPAS_BI_HAS(&bi, stack_base),  "cut: stack_base absent");
    CHECK(!SYPAS_BI_HAS(&bi, cmdline),     "cut: cmdline absent");

    /* Boundary: size covering exactly one field includes it. */
    bi.size = (uint32_t)(offsetof(sypas_bootinfo_t, stack_base)
                         + sizeof(bi.stack_base));
    CHECK(SYPAS_BI_HAS(&bi, stack_base),   "exact: stack_base present");
    CHECK(!SYPAS_BI_HAS(&bi, stack_size),  "exact: stack_size absent");

    /* The magic really is "SYPASBP1" little-endian. */
    const char expect[8] = {'S','Y','P','A','S','B','P','1'};
    uint64_t magic = SYPAS_BOOT_MAGIC;
    CHECK(memcmp(&magic, expect, 8) == 0, "magic bytes are SYPASBP1");

    /* Version constants: this is v1.0 and the header said so. */
    CHECK(SYPAS_BOOT_VERSION_MAJOR == 1 && SYPAS_BOOT_VERSION_MINOR == 0,
          "protocol version is 1.0");

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
