#ifndef NQ_CAL_H
#define NQ_CAL_H

/*
 * Axis calibration — virtual levelling for a permanently installed node.
 *
 * The node does not move once potted in rock, so the gyroscope reads zero at
 * rest and carries no orientation information. Levelling is therefore done from
 * the accelerometer's gravity vector alone (the precision ADXL355), and the
 * same rotation is applied to *every* sensor's data — including the gyro —
 * because all sensors are rigidly mounted on one board.
 *
 * Two stages compose into one matrix per sensor:
 *
 *   R_total = R_level * M_mount
 *
 *   M_mount  — fixed, known from the PCB layout: maps a chip's raw measurement
 *              axes into the common board frame. If all sensors are placed in
 *              the same orientation (the reference design's choice), this is the
 *              identity for all of them.
 *   R_level  — measured at calibration from the gravity vector: maps the board
 *              frame into the levelled frame (Z = vertical, toward Earth's core).
 *
 * Angular velocity (the gyro) is a pseudovector: under an orthogonal transform Q
 * it goes as det(Q)*Q*w, not Q*w. We bake det(M_mount) into the stored gyro
 * matrix at build time, so applying the calibration is a plain matrix*vector for
 * every sensor. With M_mount = identity (det +1), the gyro transform is
 * identical to the accelerometer's.
 */

#include "nic_math.h"
#include <stdint.h>

typedef struct {
    nic_mat3_t r;   /* R_total for this sensor; pseudovector sign already baked in */
} nq_cal_t;

/*
 * Build the levelling matrix R_level (board frame -> levelled frame) from a
 * gravity vector measured at rest, in the board frame. The vector need not be
 * unit length (pass the raw average of accelerometer samples). Returns 0 on
 * success, NIC_ERR_* if the vector is null/degenerate.
 *
 * Z of the levelled frame is aligned with the measured gravity direction; the
 * two horizontal axes are fixed by an internal reference (azimuth about vertical
 * is arbitrary — gravity cannot fix heading, and seismics do not need it).
 */
int nq_cal_level(const nic_vec3_t *gravity_board, nic_mat3_t *r_level);

/*
 * Compose the per-sensor calibration: R_total = R_level * M_mount, with the
 * pseudovector sign (det M_mount) folded in when is_pseudovector != 0 (the
 * gyro). Pass nic_mat3_identity() for M_mount when the sensor is mounted in the
 * board orientation.
 */
void nq_cal_for_sensor(const nic_mat3_t *r_level, const nic_mat3_t *m_mount,
                       int is_pseudovector, nq_cal_t *out);

/* out = R_total * in  (float in, float out). */
nic_vec3_t nq_cal_apply(const nq_cal_t *cal, const nic_vec3_t *in);

/* Convenience: transform a raw integer-count sample straight to levelled float. */
nic_vec3_t nq_cal_apply_raw(const nq_cal_t *cal, const int32_t raw[3]);

#endif /* NQ_CAL_H */
