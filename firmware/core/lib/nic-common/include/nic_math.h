#ifndef NIC_MATH_H
#define NIC_MATH_H

/*
 * Minimal 3-vector / 3x3-matrix helpers, header-only and FPU-friendly.
 * Shared by nq-cal (axis transform) and anything else that needs to rotate a
 * three-axis sample. Pure portable C — compiles on the host and the STM32H503
 * (which runs all of this in hardware via its FPU).
 */

#include <math.h>

typedef struct { float v[3]; } nic_vec3_t;
typedef struct { float m[3][3]; } nic_mat3_t;  /* row-major: m[row][col] */

static inline float nic_vec3_dot(const nic_vec3_t *a, const nic_vec3_t *b) {
    return a->v[0]*b->v[0] + a->v[1]*b->v[1] + a->v[2]*b->v[2];
}

static inline nic_vec3_t nic_vec3_cross(const nic_vec3_t *a, const nic_vec3_t *b) {
    nic_vec3_t r = {{
        a->v[1]*b->v[2] - a->v[2]*b->v[1],
        a->v[2]*b->v[0] - a->v[0]*b->v[2],
        a->v[0]*b->v[1] - a->v[1]*b->v[0],
    }};
    return r;
}

static inline float nic_vec3_len(const nic_vec3_t *a) {
    return sqrtf(nic_vec3_dot(a, a));
}

/* Normalize; returns the original length (0 if the vector was null). */
static inline float nic_vec3_normalize(const nic_vec3_t *a, nic_vec3_t *out) {
    float len = nic_vec3_len(a);
    if (len > 0.0f) {
        out->v[0] = a->v[0]/len; out->v[1] = a->v[1]/len; out->v[2] = a->v[2]/len;
    } else {
        out->v[0] = out->v[1] = out->v[2] = 0.0f;
    }
    return len;
}

/* out = a - s*b */
static inline nic_vec3_t nic_vec3_madd(const nic_vec3_t *a, float s, const nic_vec3_t *b) {
    nic_vec3_t r = {{ a->v[0]-s*b->v[0], a->v[1]-s*b->v[1], a->v[2]-s*b->v[2] }};
    return r;
}

static inline nic_mat3_t nic_mat3_identity(void) {
    nic_mat3_t r = {{ {1,0,0}, {0,1,0}, {0,0,1} }};
    return r;
}

/* out = A * B */
static inline nic_mat3_t nic_mat3_mul(const nic_mat3_t *a, const nic_mat3_t *b) {
    nic_mat3_t r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r.m[i][j] = a->m[i][0]*b->m[0][j]
                      + a->m[i][1]*b->m[1][j]
                      + a->m[i][2]*b->m[2][j];
    return r;
}

/* out = A * v */
static inline nic_vec3_t nic_mat3_apply(const nic_mat3_t *a, const nic_vec3_t *v) {
    nic_vec3_t r;
    for (int i = 0; i < 3; i++)
        r.v[i] = a->m[i][0]*v->v[0] + a->m[i][1]*v->v[1] + a->m[i][2]*v->v[2];
    return r;
}

static inline float nic_mat3_det(const nic_mat3_t *a) {
    return a->m[0][0]*(a->m[1][1]*a->m[2][2] - a->m[1][2]*a->m[2][1])
         - a->m[0][1]*(a->m[1][0]*a->m[2][2] - a->m[1][2]*a->m[2][0])
         + a->m[0][2]*(a->m[1][0]*a->m[2][1] - a->m[1][1]*a->m[2][0]);
}

static inline nic_mat3_t nic_mat3_scale(const nic_mat3_t *a, float s) {
    nic_mat3_t r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r.m[i][j] = a->m[i][j]*s;
    return r;
}

#endif /* NIC_MATH_H */
