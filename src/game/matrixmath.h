#ifndef MATRIXMATH_H
#define MATRIXMATH_H

#include <ultra64.h>
#include <bondtypes.h>

void mtx4RotateVecInPlace(Mtxf *matrix, struct coord3d *vector);
void matrix_4x4_set_lookat_target(Mtxf *arg0, f32 arg1, f32 arg2, f32 arg3, f32 arg4, f32 arg5, f32 arg6, f32 arg7, f32 arg8, f32 arg9);

void matrix_4x4_multiply(Mtxf *lhs, Mtxf *rhs, Mtxf *result);
void matrix_4x4_set_position(struct coord3d *position, Mtxf *matrix);
void matrix_4x4_set_lookat(Mtxf *arg0, f32 arg1, f32 arg2, f32 arg3, f32 arg4, f32 arg5, f32 arg6, f32 arg7, f32 arg8, f32 arg9);
u32 matrix_4x4_calc_depth_scale(f32 arg0, f32 arg1);
void matrix_4x4_calc_adjugate(Mtxf *arg0, Mtxf *arg1);

f32  matrix_4x4_determinant(Mtxf *matrix);
f32  matrix_3x3_determinant(f32 a, f32 d, f32 g, f32 b, f32 e, f32 h, f32 c, f32 f, f32 i);
f32  matrix_2x2_determinant(f32 a, f32 c, f32 b, f32 d);

void matrix_4x4_7F058C4C(f32 arg0);
void matrix_4x4_copy(Mtxf *src, Mtxf *dst);
void matrix_4x4_multiply_homogeneous(Mtxf *lhs, Mtxf *rhs, Mtxf *result);

void matrix_4x4_f32_to_s32(f32 mf[4][4], s32 ms[4][4]);
void matrix_4x4_multiply_homogeneous_in_place(Mtxf *lhs, Mtxf *rhs);
void mtx4TransformVecInPlace(Mtxf *matrix, struct coord3d *vector);
void matrix_4x4_set_identity(Mtxf *matrix);
void matrix_4x4_set_rotation_around_x(f32 angle, Mtxf *matrix);
void matrix_4x4_set_rotation_around_y(f32 angle, Mtxf *matrix);
void matrix_scalar_multiply(f32 scalar, f32 *matrix);
void matrix_4x4_set_basis_and_position_target(Mtxf *arg0, f32 arg1, f32 arg2, f32 arg3, f32 arg4, f32 arg5, f32 arg6, f32 arg7, f32 arg8, f32 arg9);
void matrix_4x4_set_rotation_around_z(f32 angle, Mtxf *matrix);
void matrix_4x4_multiply_in_place(Mtxf *lhs, Mtxf *rhs);
void matrix_4x4_set_identity_and_position(struct coord3d *position, Mtxf *matrix);
void matrix_4x4_set_position_and_rotation_around_y(f32 *position, f32 angle, Mtxf *matrix);
void matrix_scalar_multiply_2(f32 scalar, f32 *matrix);
void matrix_4x4_set_rotation_around_xyz(struct coord3d *angles, Mtxf *matrix);
void matrix_4x4_get_rotation_around_xyz(Mtxf *matrix, struct coord3d *angles);
void matrix_4x4_set_basis_and_position(Mtxf *arg0, f32 arg1, f32 arg2, f32 arg3, f32 arg4, f32 arg5, f32 arg6, f32 arg7, f32 arg8, f32 arg9);
void matrix_4x4_set_inverse_rotation_and_translation(Mtxf *arg0, Mtxf *arg1);
void matrix_column_1_scalar_multiply(f32 scalar, f32 *matrix);
void matrix_column_2_scalar_multiply(f32 scalar, f32 *matrix);
void matrix_column_3_scalar_multiply(f32 scalar, f32 *matrix);
void matrix_column_3_scalar_multiply_2(f32 scalar, f32 *matrix);
void matrix_4x4_set_position_and_rotation_around_xyz(struct coord3d *position, struct coord3d * rotation, Mtxf *matrix);
void matrix_scalar_multiply_3(f32 scalar, f32 *matrix);
void matrix_4x4_align(Mtxf *matrix, f32 angle, f32 x, f32 y, f32 z);
void matrix_4x4_set_rotation_axis_angle(Mtxf *matrix, f32 angle, f32 x, f32 y, f32 z);
void matrix_4x4_set_rotation_inverse(Mtxf *rotation, Mtxf *transpose);
void matrix_4x4_transform_vector(Mtxf *matrix, struct coord3d *vector, struct coord3d *result);
void matrix_row_3_scalar_multiply(f32 scalar, f32 *matrix);

// tenative guess
void sub_GAME_7F058E78(Mtxf *arg0, Mtxf *arg1);
void matrix_7f05842c_eu(f32 src[][4], f32 dst[3][3]);
void matrix_4x4_multiply_homogeneous_in_place_eu(f32 src[3][3], f32 dst[3][3]);
void matrix_4x4_multiply_homogeneous_eu(f32 lhs[3][3], f32 rhs[3][3], f32 result[3][3]);

/* matrixmath_misc.h */

void vec3Lerp(vec3d *x, vec3d *y, f32 scaler, vec3d *result);
void coord3dCubicSplineInterp(coord3d *prev, coord3d *start, coord3d *end, coord3d *next, f32 fraction, f32 tangentScale, coord3d *result);
void coord3dCatmullRomInterp(coord3d *p0, coord3d *p1, coord3d *p2, coord3d *p3, f32 fraction, coord3d *result);

void sub_GAME_7F057DF8(Mtxf *arg0, Mtxf *arg1, s32 count);
void mtxLoadRandomRotation(Mtxf *mtx);
void sub_GAME_7F059334(Mtx *src, Mtx *dst);
void matrix_4x4_7F058C64(void);
void matrix_4x4_7F058C88(void);
void matrix_4x4_invert_affine(Mtxf *matrix, Mtxf *result);

#endif
