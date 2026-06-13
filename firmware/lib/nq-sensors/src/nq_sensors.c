#include "nq_sensors.h"
#include "nq_icm42688.h"
#include "nq_adxl355.h"
#include "nq_scl3300.h"

const char *nq_sensor_name(nq_sensor_id_t id) {
    switch (id) {
        case NQ_SENSOR_ICM42688: return "ICM-42688-P";
        case NQ_SENSOR_ADXL355:  return "ADXL355";
        case NQ_SENSOR_SCL3300:  return "SCL3300";
        default:                 return "?";
    }
}

int nq_sensors_detect(const nq_sensors_buses_t *buses, nq_sensors_map_t *out) {
    for (int i = 0; i < NQ_SENSOR_COUNT; i++) {
        out->present[i] = 0;
    }
    out->count = 0;

    /* A negative probe return is a bus error -> treat as absent, keep going. */
    out->present[NQ_SENSOR_ICM42688] = (nq_icm42688_probe(&buses->icm)     == 1);
    out->present[NQ_SENSOR_ADXL355]  = (nq_adxl355_probe(&buses->adxl355)  == 1);
    out->present[NQ_SENSOR_SCL3300]  = (nq_scl3300_probe(&buses->scl3300)  == 1);

    for (int i = 0; i < NQ_SENSOR_COUNT; i++) {
        out->count += out->present[i];
    }
    return NIC_OK;
}

nq_sensor_id_t nq_sensors_seismic_source(const nq_sensors_map_t *map) {
    if (map->present[NQ_SENSOR_ADXL355])  return NQ_SENSOR_ADXL355;   /* the better one */
    if (map->present[NQ_SENSOR_ICM42688]) return NQ_SENSOR_ICM42688;  /* fall back */
    return NQ_SENSOR_COUNT;                                           /* none */
}
