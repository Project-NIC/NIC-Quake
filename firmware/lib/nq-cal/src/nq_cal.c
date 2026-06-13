#include "nq_cal.h"
#include "nic_types.h"

int nq_cal_level(const nic_vec3_t *gravity_board, nic_mat3_t *r_level) {
    /* Z axis of the levelled frame = measured gravity direction. */
    nic_vec3_t z;
    if (nic_vec3_normalize(gravity_board, &z) <= 0.0f) {
        return NIC_ERR_ABSENT;   /* null vector: no usable gravity reading */
    }

    /* Pick a reference axis least aligned with Z to avoid a degenerate cross
     * product, then Gram-Schmidt it into the horizontal plane -> X axis. */
    nic_vec3_t ref = {{ 1.0f, 0.0f, 0.0f }};
    if (fabsf(z.v[0]) > 0.9f) {
        ref.v[0] = 0.0f; ref.v[1] = 1.0f; ref.v[2] = 0.0f;
    }
    float proj = nic_vec3_dot(&ref, &z);
    nic_vec3_t x_raw = nic_vec3_madd(&ref, proj, &z);   /* ref - (ref.z) z */
    nic_vec3_t x;
    if (nic_vec3_normalize(&x_raw, &x) <= 0.0f) {
        return NIC_ERR;          /* should not happen given the ref choice */
    }

    /* Y = Z x X completes a right-handed orthonormal basis (det +1). */
    nic_vec3_t y = nic_vec3_cross(&z, &x);

    /* Rows are the levelled axes expressed in the board frame, so R*v gives v's
     * components along (X, Y, Z) of the levelled frame. */
    for (int j = 0; j < 3; j++) {
        r_level->m[0][j] = x.v[j];
        r_level->m[1][j] = y.v[j];
        r_level->m[2][j] = z.v[j];
    }
    return NIC_OK;
}

void nq_cal_for_sensor(const nic_mat3_t *r_level, const nic_mat3_t *m_mount,
                       int is_pseudovector, nq_cal_t *out) {
    nic_mat3_t total = nic_mat3_mul(r_level, m_mount);
    if (is_pseudovector) {
        /* Angular velocity transforms as det(Q)*Q*w. R_level is a proper
         * rotation (det +1), so the sign is det(M_mount). Fold it in once. */
        float d = nic_mat3_det(m_mount);
        if (d < 0.0f) {
            total = nic_mat3_scale(&total, -1.0f);
        }
    }
    out->r = total;
}

nic_vec3_t nq_cal_apply(const nq_cal_t *cal, const nic_vec3_t *in) {
    return nic_mat3_apply(&cal->r, in);
}

nic_vec3_t nq_cal_apply_raw(const nq_cal_t *cal, const int32_t raw[3]) {
    nic_vec3_t v = {{ (float)raw[0], (float)raw[1], (float)raw[2] }};
    return nic_mat3_apply(&cal->r, &v);
}
