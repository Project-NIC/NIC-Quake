/* Host-side test for nq-lora: the 16-byte master telemetry frame round-trips and
 * the CRC / magic / capacity guards hold. */

#include "nic_lora.h"

#include <stdio.h>

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); g_failures++; } \
} while (0)

int main(void) {
    printf("test_lora\n");

    nic_lora_status_t s = { .master = 1, .unix_s = 0x12345678u, .volt_dv = 120,
                           .temp_c = -5, .events = 4242, .alive = 0xA5,
                           .status = 0x42, .peak = 60000 };
    uint8_t buf[NIC_LORA_LEN];
    CHECK(nic_lora_pack(&s, buf, sizeof buf) == (int)NIC_LORA_LEN);
    CHECK(buf[0] == NIC_LORA_MAGIC);

    nic_lora_status_t r;
    CHECK(nic_lora_unpack(buf, sizeof buf, &r) == (int)NIC_LORA_LEN);
    CHECK(r.master == 1 && r.unix_s == 0x12345678u && r.volt_dv == 120 && r.temp_c == -5);
    CHECK(r.events == 4242 && r.alive == 0xA5 && r.status == 0x42 && r.peak == 60000);

    /* CRC catches a single-bit flip */
    buf[7] ^= 0x01u;
    CHECK(nic_lora_unpack(buf, sizeof buf, &r) < 0);
    buf[7] ^= 0x01u;

    /* bad magic / short buffer rejected */
    buf[0] = 0x00u;
    CHECK(nic_lora_unpack(buf, sizeof buf, &r) < 0);
    CHECK(nic_lora_pack(&s, buf, 8) < 0);

    if (g_failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
