#ifndef NIC_SPI_H
#define NIC_SPI_H

/*
 * Hardware-agnostic SPI bus handle.
 *
 * Drivers (nq-icm42688, nq-adxl355, ...) never touch STM32 registers. They
 * talk to a sensor through this handle. The glue layer (nq-node) creates one
 * handle per physical SPI peripheral and fills in `transfer`. On a host PC the
 * same handle is filled with a mock so the drivers can be unit-tested without
 * any hardware.
 *
 * One sensor = one SPI bus, no sharing (SPI1=ICM, SPI2=ADXL355, SPI3=SCL3300),
 * so the handle carries no chip-select multiplexing — CS is implied per bus.
 */

#include <stdint.h>
#include <stddef.h>

typedef struct nic_spi {
    /* Opaque context, e.g. a pointer to the SPI peripheral / CS descriptor.
     * Passed straight back to `transfer`; the drivers never look inside it. */
    void *ctx;

    /*
     * Full-duplex transfer of `len` bytes. CS is asserted for the whole call.
     * `tx`==NULL sends zero bytes; `rx`==NULL discards the read data; tx and rx
     * may alias. Returns 0 on success, negative on error.
     */
    int (*transfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);
} nic_spi_t;

static inline int nic_spi_transfer(const nic_spi_t *bus,
                                  const uint8_t *tx, uint8_t *rx, size_t len) {
    return bus->transfer(bus->ctx, tx, rx, len);
}

#endif /* NIC_SPI_H */
