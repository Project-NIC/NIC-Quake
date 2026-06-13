#include "nq_scl3300.h"

/* Murata SCL3300 CRC-8: poly 0x1D, init 0xFF, over bits [31:8], result inverted. */
static uint8_t scl_crc8(uint32_t data) {
    uint8_t crc = 0xFFu;
    for (int b = 31; b >= 8; b--) {
        uint8_t bit = (uint8_t)((data >> b) & 1u);
        uint8_t msb = (uint8_t)(crc & 0x80u);
        if (bit) msb ^= 0x80u;
        crc = (uint8_t)(crc << 1);
        if (msb) crc ^= 0x1Du;
    }
    return (uint8_t)(~crc);
}

/* One 32-bit exchange, MSB-first. The response answers the PREVIOUS command. */
static int scl_xfer(const nic_spi_t *bus, uint32_t cmd, uint32_t *resp) {
    uint8_t tx[4] = { (uint8_t)(cmd >> 24), (uint8_t)(cmd >> 16),
                      (uint8_t)(cmd >> 8),  (uint8_t)cmd };
    uint8_t rx[4] = { 0 };
    int r = nic_spi_transfer(bus, tx, rx, sizeof tx);
    if (r != NIC_OK) {
        return r;
    }
    *resp = ((uint32_t)rx[0] << 24) | ((uint32_t)rx[1] << 16)
          | ((uint32_t)rx[2] << 8)  |  (uint32_t)rx[3];
    return NIC_OK;
}

/*
 * Read a register: send the command twice (pipelined), use the second
 * response, verify its CRC, return the 16-bit data as a signed value.
 */
static int scl_read(const nic_spi_t *bus, uint32_t cmd, int16_t *data) {
    uint32_t resp;
    int r;
    if ((r = scl_xfer(bus, cmd, &resp)) != NIC_OK) return r;   /* prime (discard) */
    if ((r = scl_xfer(bus, cmd, &resp)) != NIC_OK) return r;   /* carries the data */
    if (scl_crc8(resp) != (uint8_t)(resp & 0xFFu)) {
        return NIC_ERR_CRC;
    }
    *data = (int16_t)((resp >> 8) & 0xFFFFu);
    return NIC_OK;
}

static int scl_read3(const nic_spi_t *bus, uint32_t cx, uint32_t cy, uint32_t cz,
                     nic_sample3_t *out) {
    int16_t x, y, z;
    int r;
    if ((r = scl_read(bus, cx, &x)) != NIC_OK) return r;
    if ((r = scl_read(bus, cy, &y)) != NIC_OK) return r;
    if ((r = scl_read(bus, cz, &z)) != NIC_OK) return r;
    out->axis[0] = x;
    out->axis[1] = y;
    out->axis[2] = z;
    return NIC_OK;
}

int nq_scl3300_probe(const nic_spi_t *bus) {
    int16_t id;
    int r = scl_read(bus, NQ_SCL3300_CMD_WHOAMI, &id);
    if (r != NIC_OK) {
        return r;
    }
    return ((id & 0xFF) == NQ_SCL3300_WHOAMI_VALUE) ? 1 : 0;
}

int nq_scl3300_init(const nic_spi_t *bus, uint32_t mode_cmd) {
    uint32_t resp;
    int16_t status;
    int r;

    if ((r = scl_xfer(bus, NQ_SCL3300_CMD_SWRESET, &resp)) != NIC_OK) return r;
    /* board waits ~1 ms */
    if ((r = scl_xfer(bus, mode_cmd, &resp)) != NIC_OK) return r;
    if ((r = scl_xfer(bus, NQ_SCL3300_CMD_ENABLE_ANGLE, &resp)) != NIC_OK) return r;
    /* board waits ~100 ms for the angle output to settle */
    if ((r = scl_read(bus, NQ_SCL3300_CMD_READ_STATUS, &status)) != NIC_OK) return r;
    (void)status;   /* read once to clear the status summary */
    return NIC_OK;
}

int nq_scl3300_init_auto(const nic_spi_t *bus, int *near_level) {
    int r;
    nic_sample3_t g;

    if ((r = nq_scl3300_init(bus, NQ_SCL3300_CMD_MODE1)) != NIC_OK) return r;
    if ((r = nq_scl3300_read_acc(bus, &g)) != NIC_OK) return r;

    int64_t x = g.axis[0], y = g.axis[1], z = g.axis[2];
    int64_t hor2 = x*x + y*y;
    int64_t tot2 = hor2 + z*z;
    /* near level <=> sin^2(tilt) = hor2/tot2 <= sin^2(10°) ≈ 0.03015 */
    int near = (tot2 > 0) && (hor2 * 100000 <= 3015 * tot2);

    if (near) {
        if ((r = nq_scl3300_init(bus, NQ_SCL3300_CMD_MODE4)) != NIC_OK) return r;
    }
    if (near_level) {
        *near_level = near ? 1 : 0;
    }
    return NIC_OK;
}

int nq_scl3300_read_acc(const nic_spi_t *bus, nic_sample3_t *out) {
    return scl_read3(bus, NQ_SCL3300_CMD_READ_ACC_X,
                          NQ_SCL3300_CMD_READ_ACC_Y,
                          NQ_SCL3300_CMD_READ_ACC_Z, out);
}

int nq_scl3300_read_ang(const nic_spi_t *bus, nic_sample3_t *out) {
    return scl_read3(bus, NQ_SCL3300_CMD_READ_ANG_X,
                          NQ_SCL3300_CMD_READ_ANG_Y,
                          NQ_SCL3300_CMD_READ_ANG_Z, out);
}

int nq_scl3300_read_temp(const nic_spi_t *bus, int16_t *out) {
    return scl_read(bus, NQ_SCL3300_CMD_READ_TEMP, out);
}
