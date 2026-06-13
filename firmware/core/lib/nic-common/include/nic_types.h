#ifndef NIC_TYPES_H
#define NIC_TYPES_H

/*
 * Common data types shared across NIC platform firmware libraries.
 *
 * Everything here is raw — the node receives, packs and sends RAW counts. Any
 * scaling to physical units, or the axis transform from calibration, happens
 * either in nq-cal (rotation matrix) or off-node on the master.
 */

#include <stdint.h>

/*
 * One three-axis sample, raw sensor counts, sign-extended.
 * int32_t holds every sensor in the design: ICM-42688 is 16-bit, the ADXL355
 * family is 20-bit. Axis order is the sensor's native X, Y, Z (pre-transform).
 */
typedef struct {
    int32_t axis[3];
} nic_sample3_t;

/* Standard return convention: 0 = OK, negative = error. */
#define NIC_OK        (0)
#define NIC_ERR      (-1)   /* generic / bus error                */
#define NIC_ERR_ID   (-2)   /* device present but wrong ID        */
#define NIC_ERR_ABSENT (-3) /* no device responded on the bus     */
#define NIC_ERR_CRC    (-4) /* frame failed its CRC check         */
#define NIC_ERR_FORMAT (-5) /* malformed / truncated / bad sync   */

#endif /* NIC_TYPES_H */
