/*
 * Host-side test for nq-cal axis calibration.
 *
 * Pure math, no hardware: feed a known gravity vector, check the levelling
 * matrix maps it onto Z, and that the gyro (pseudovector) handling behaves.
 */

#include "nq_cal.h"
#include "nic_math.h"

#include <stdio.h>
#include <math.h>

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); g_failures++; } \
} while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-3f)

static void test_level_puts_gravity_on_z(void) {
    printf("test_level_puts_gravity_on_z\n");
    nic_vec3_t g = {{ 3.0f, 4.0f, 12.0f }};   /* len 13, arbitrary tilt */
    nic_mat3_t R;
    CHECK(nq_cal_level(&g, &R) == 0);
    nic_vec3_t lg = nic_mat3_apply(&R, &g);
    CHECK(NEAR(lg.v[0], 0.0f));
    CHECK(NEAR(lg.v[1], 0.0f));
    CHECK(NEAR(lg.v[2], 13.0f));             /* gravity now purely vertical */
}

static void test_level_is_proper_rotation(void) {
    printf("test_level_is_proper_rotation\n");
    nic_vec3_t g = {{ -1.0f, 2.0f, 5.0f }};
    nic_mat3_t R;
    nq_cal_level(&g, &R);
    for (int i = 0; i < 3; i++)             /* R * R^T = I (orthonormal) */
        for (int j = 0; j < 3; j++) {
            float d = R.m[i][0]*R.m[j][0] + R.m[i][1]*R.m[j][1] + R.m[i][2]*R.m[j][2];
            CHECK(NEAR(d, i == j ? 1.0f : 0.0f));
        }
    CHECK(NEAR(nic_mat3_det(&R), 1.0f));       /* proper rotation, not a reflection */
}

static void test_gyro_identity_mount_equals_accel(void) {
    printf("test_gyro_identity_mount_equals_accel\n");
    nic_vec3_t g = {{ 0.0f, 0.0f, 1.0f }};
    nic_mat3_t R; nq_cal_level(&g, &R);
    nic_mat3_t I = nic_mat3_identity();
    nq_cal_t accel, gyro;
    nq_cal_for_sensor(&R, &I, 0, &accel);
    nq_cal_for_sensor(&R, &I, 1, &gyro);     /* pseudovector, but det I = +1 */
    nic_vec3_t w = {{ 1.0f, 2.0f, 3.0f }};
    nic_vec3_t a = nq_cal_apply(&accel, &w);
    nic_vec3_t b = nq_cal_apply(&gyro, &w);
    CHECK(NEAR(a.v[0], b.v[0]) && NEAR(a.v[1], b.v[1]) && NEAR(a.v[2], b.v[2]));
}

static void test_vertical_rate_lands_on_z(void) {
    printf("test_vertical_rate_lands_on_z\n");
    nic_vec3_t g = {{ 3.0f, 4.0f, 12.0f }};
    nic_mat3_t R; nq_cal_level(&g, &R);
    nic_mat3_t I = nic_mat3_identity();
    nq_cal_t gyro; nq_cal_for_sensor(&R, &I, 1, &gyro);
    nic_vec3_t w = {{ 3.0f, 4.0f, 12.0f }};   /* rate parallel to gravity */
    nic_vec3_t lw = nq_cal_apply(&gyro, &w);
    CHECK(NEAR(lw.v[0], 0.0f) && NEAR(lw.v[1], 0.0f) && NEAR(lw.v[2], 13.0f));
}

static void test_pseudovector_reflection_sign(void) {
    printf("test_pseudovector_reflection_sign\n");
    nic_vec3_t g = {{ 0.0f, 0.0f, 1.0f }};
    nic_mat3_t R; nq_cal_level(&g, &R);
    nic_mat3_t M = nic_mat3_identity(); M.m[2][2] = -1.0f;  /* a reflection, det -1 */
    CHECK(NEAR(nic_mat3_det(&M), -1.0f));
    nq_cal_t accel, gyro;
    nq_cal_for_sensor(&R, &M, 0, &accel);    /* true vector */
    nq_cal_for_sensor(&R, &M, 1, &gyro);     /* pseudovector: extra sign flip */
    nic_vec3_t v = {{ 1.0f, 2.0f, 3.0f }};
    nic_vec3_t a = nq_cal_apply(&accel, &v);
    nic_vec3_t b = nq_cal_apply(&gyro, &v);
    CHECK(NEAR(b.v[0], -a.v[0]) && NEAR(b.v[1], -a.v[1]) && NEAR(b.v[2], -a.v[2]));
}

static void test_apply_raw_matches_float(void) {
    printf("test_apply_raw_matches_float\n");
    nic_vec3_t g = {{ 0.2f, -0.3f, 0.9f }};
    nic_mat3_t R; nq_cal_level(&g, &R);
    nic_mat3_t I = nic_mat3_identity();
    nq_cal_t c; nq_cal_for_sensor(&R, &I, 0, &c);
    int32_t raw[3] = { 1000, -2000, 30000 };
    nic_vec3_t f = {{ 1000.0f, -2000.0f, 30000.0f }};
    nic_vec3_t r1 = nq_cal_apply_raw(&c, raw);
    nic_vec3_t r2 = nq_cal_apply(&c, &f);
    CHECK(NEAR(r1.v[0], r2.v[0]) && NEAR(r1.v[1], r2.v[1]) && NEAR(r1.v[2], r2.v[2]));
}

static void test_null_gravity_rejected(void) {
    printf("test_null_gravity_rejected\n");
    nic_vec3_t g = {{ 0.0f, 0.0f, 0.0f }};
    nic_mat3_t R;
    CHECK(nq_cal_level(&g, &R) != 0);
}

int main(void) {
    test_level_puts_gravity_on_z();
    test_level_is_proper_rotation();
    test_gyro_identity_mount_equals_accel();
    test_vertical_rate_lands_on_z();
    test_pseudovector_reflection_sign();
    test_apply_raw_matches_float();
    test_null_gravity_rejected();

    if (g_failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
