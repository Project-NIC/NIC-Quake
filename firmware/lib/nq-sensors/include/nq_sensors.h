#ifndef NQ_SENSORS_H
#define NQ_SENSORS_H

/*
 * Sensor manager: startup auto-detection over the three dedicated SPI buses,
 * and (later) a single sample API over whichever sensors are populated.
 *
 * The same firmware binary runs on a node with one, two or three sensors. At
 * boot each bus is probed for its sensor's identity register; a sensor that
 * answers is marked present, one that stays silent is skipped. The data packet
 * sent to the master then carries only the present sensors — the master knows
 * the node's schema and expects exactly that.
 *
 * Wiring is fixed (one sensor per bus), so the bus identity is the sensor
 * identity.
 */

#include "nic_spi.h"
#include "nic_types.h"

typedef enum {
    NQ_SENSOR_ICM42688 = 0,  /* SPI1 — 6-axis IMU                      */
    NQ_SENSOR_ADXL355,       /* SPI2 — precision seismic accelerometer */
    NQ_SENSOR_SCL3300,       /* SPI3 — inclinometer / drift reference  */
    NQ_SENSOR_COUNT
} nq_sensor_id_t;

/* The three dedicated buses, supplied by the glue layer. */
typedef struct {
    nic_spi_t icm;       /* SPI1 */
    nic_spi_t adxl355;   /* SPI2 */
    nic_spi_t scl3300;   /* SPI3 */
} nq_sensors_buses_t;

/* Detection result: present[id] is 1 if that sensor answered, else 0. */
typedef struct {
    int present[NQ_SENSOR_COUNT];
    int count;          /* number of sensors present */
} nq_sensors_map_t;

/*
 * Probe all three buses and fill `out`. A bus-level error on one probe does not
 * abort the others — that sensor is simply marked absent. Always returns NIC_OK;
 * inspect the map for what was found.
 */
int nq_sensors_detect(const nq_sensors_buses_t *buses, nq_sensors_map_t *out);

/* Human-readable name for logging / diagnostics. */
const char *nq_sensor_name(nq_sensor_id_t id);

/*
 * Which accelerometer feeds the seismic stream and the levelling gravity vector:
 * prefer the precision ADXL355, fall back to the ICM-42688's (noisier) accel if
 * the ADXL355 is not populated, else NQ_SENSOR_COUNT when neither is present.
 * (The SCL3300 is the slow tilt/drift reference, not this source — it is ~20x
 * noisier in raw acceleration and not clock-synced. Its Mode 1 ±1.2g / ±90°
 * range does handle any install tilt, so it could level in a pinch, but the
 * ADXL355/ICM are the better choice for the high-rate seismic path.)
 */
nq_sensor_id_t nq_sensors_seismic_source(const nq_sensors_map_t *map);

#endif /* NQ_SENSORS_H */
