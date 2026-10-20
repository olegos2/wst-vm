# Ray marches mandelbulb.glsl into the vm framebuffer. C reference:
# mandelbulb_ref.c. Single precision; trig/log/pow are software
# polynomials, sqrt is hardware. vec3 arguments/results use f0-f2,
# scalar args f3-f4. f32 locals occupy 8-byte slots via fsw/flw.

section .text

# void print(const char *msg)
# write(2, msg, *(u64 *)(msg - 8))
:print
    pushqr   x4
    ld       x4, x1, -1
    addi     x3, x1, 0
    addi     x2, x0, 2          # stderr
    addi     x1, x0, 1          # write
    ecall
    popqr    x4
    ret

# void exit(int code)
# sys_exit(code)
:exit
    addi     x1, x0, 0          # exit
    ecall

# ---
# Vector arithmetic

# vec3 v_add(vec3 a, vec3 b)
# a += b
:v_add
    fadd.s   f0, f0, f3
    fadd.s   f1, f1, f4
    fadd.s   f2, f2, f5
    ret

# vec3 v_sub(vec3 a, vec3 b)
# a -= b
:v_sub
    fsub.s   f0, f0, f3
    fsub.s   f1, f1, f4
    fsub.s   f2, f2, f5
    ret

# vec3 v_scale(vec3 a, float s)
# a *= s
:v_scale
    fmul.s   f0, f0, f3
    fmul.s   f1, f1, f3
    fmul.s   f2, f2, f3
    ret

# vec3 v_mul(vec3 a, vec3 b)
# a *= b
:v_mul
    fmul.s   f0, f0, f3
    fmul.s   f1, f1, f4
    fmul.s   f2, f2, f5
    ret

# vec3 mix3(vec3 a, vec3 b, float t)
# a = mix(a, b, t)
:mix3
    fsub.s   f7, f3, f0
    fmul.s   f7, f7, f6
    fadd.s   f0, f0, f7
    fsub.s   f7, f4, f1
    fmul.s   f7, f7, f6
    fadd.s   f1, f1, f7
    fsub.s   f7, f5, f2
    fmul.s   f7, f7, f6
    fadd.s   f2, f2, f7
    ret

# float v_dot(vec3 a, vec3 b)
# return dot(a, b)
:v_dot
    fmul.s   f6, f0, f3
    fmul.s   f7, f1, f4
    fadd.s   f6, f6, f7
    fmul.s   f7, f2, f5
    fadd.s   f0, f6, f7
    ret

# float v_len(vec3 a)
# return length(a)
:v_len
    fmul.s   f3, f0, f0
    fmul.s   f4, f1, f1
    fadd.s   f3, f3, f4
    fmul.s   f4, f2, f2
    fadd.s   f3, f3, f4
    fsqrt.s  f0, f3
    ret

# vec3 v_norm(vec3 a)
# return normalize(a)
:v_norm
    fmul.s   f3, f0, f0
    fmul.s   f4, f1, f1
    fadd.s   f3, f3, f4
    fmul.s   f4, f2, f2
    fadd.s   f3, f3, f4
    fsqrt.s  f3, f3
    flw      f4, x0, :c_one
    fdiv.s   f4, f4, f3
    fmul.s   f0, f0, f4
    fmul.s   f1, f1, f4
    fmul.s   f2, f2, f4
    ret

# ---
# Math function approximations

# void sincos(float angle)
# f4 = sin(angle), f5 = cos(angle)
# reduce to [-pi/4, pi/4], evaluate polynomials, adjust quadrant
:sincos
    flw      f6, x0, :c_2_over_pi
    fmul.s   f6, f3, f6
    flw      f7, x0, :c_half
    flw      f4, x0, :c_zero
    flt.s    x2, f6, f4
    beq      x2, x0, :sc_round
    fneg.s   f7, f7
:sc_round
    fadd.s   f6, f6, f7
    fcvt.w.s x2, f6
    fcvt.s.w f6, x2
    flw      f7, x0, :c_pi_2
    fmul.s   f6, f6, f7
    fsub.s   f7, f3, f6         # r
    fmul.s   f6, f7, f7         # r*r

    # sin(r) = r * (1 + r2 * (c3 + r2 * (c5 + ...)))
    flw      f4, x0, :c_sin_11
    flw      f5, x0, :c_sin_9
    fmadd.s  f4, f4, f6, f5
    flw      f5, x0, :c_sin_7
    fmadd.s  f4, f4, f6, f5
    flw      f5, x0, :c_sin_5
    fmadd.s  f4, f4, f6, f5
    flw      f5, x0, :c_sin_3
    fmadd.s  f4, f4, f6, f5
    flw      f5, x0, :c_sin_1
    fmadd.s  f4, f4, f6, f5
    fmul.s   f4, f4, f7

    # cos(r) = 1 + r2 * (c2 + r2 * (c4 + ...))
    flw      f5, x0, :c_cos_12
    flw      f3, x0, :c_cos_10
    fmadd.s  f5, f5, f6, f3
    flw      f3, x0, :c_cos_8
    fmadd.s  f5, f5, f6, f3
    flw      f3, x0, :c_cos_6
    fmadd.s  f5, f5, f6, f3
    flw      f3, x0, :c_cos_4
    fmadd.s  f5, f5, f6, f3
    flw      f3, x0, :c_cos_2
    fmadd.s  f5, f5, f6, f3
    flw      f3, x0, :c_cos_0
    fmadd.s  f5, f5, f6, f3

    andi     x3, x2, 3
    beq      x3, x0, :sc_done
    addi     x1, x0, 1
    beq      x3, x1, :sc_q1
    addi     x1, x0, 2
    beq      x3, x1, :sc_q2
    # q3: sin = -cos, cos = sin
    fmv.s    f3, f4
    fneg.s   f4, f5
    fmv.s    f5, f3
    ret
:sc_q2
    fneg.s   f4, f4
    fneg.s   f5, f5
    ret
:sc_q1
    # q1: sin = cos, cos = -sin
    fmv.s    f3, f4
    fmv.s    f4, f5
    fneg.s   f5, f3
:sc_done
    ret

# float atanf(float x)
# return polynomial atan(x), |x| <= 1
:atanf
    fmv.s    f6, f0
    fmul.s   f1, f0, f0
    flw      f2, x0, :c_atan_7
    flw      f3, x0, :c_atan_6
    fmadd.s  f2, f2, f1, f3
    flw      f3, x0, :c_atan_5
    fmadd.s  f2, f2, f1, f3
    flw      f3, x0, :c_atan_4
    fmadd.s  f2, f2, f1, f3
    flw      f3, x0, :c_atan_3
    fmadd.s  f2, f2, f1, f3
    flw      f3, x0, :c_atan_2
    fmadd.s  f2, f2, f1, f3
    flw      f3, x0, :c_atan_1
    fmadd.s  f2, f2, f1, f3
    flw      f3, x0, :c_atan_0
    fmadd.s  f2, f2, f1, f3
    fmul.s   f0, f6, f2
    ret

# float atan2f(float y, float x)
# return atan2(y, x)
:atan2f
    addi     x7, x7, -32
    fsw      x7, f0, 0
    fsw      x7, f1, 2

    flw      f2, x0, :c_zero
    feq.s    x2, f0, f2
    feq.s    x3, f1, f2
    and      x2, x2, x3
    bne      x2, x0, :atan2_zero

    fabs.s   f2, f0
    fabs.s   f3, f1
    flt.s    x2, f3, f2
    bne      x2, x0, :atan2_ybig

    # |x| >= |y|: atan(y/x), add pi in the left half-plane.
    flw      f0, x7, 0
    flw      f1, x7, 2
    fdiv.s   f0, f0, f1
    call     :atanf
    flw      f1, x7, 2
    flw      f2, x0, :c_zero
    flt.s    x2, f1, f2
    beq      x2, x0, :atan2_done
    flw      f1, x7, 0
    flt.s    x2, f1, f2
    bne      x2, x0, :atan2_neg_pi
    flw      f1, x0, :c_pi
    fadd.s   f0, f0, f1
    jal      x0, :atan2_done
:atan2_neg_pi
    flw      f1, x0, :c_neg_pi
    fadd.s   f0, f0, f1
    jal      x0, :atan2_done

:atan2_ybig
    # |y| > |x|: atan(x/y), then pi/2 - a or -pi/2 - a.
    flw      f0, x7, 2
    flw      f1, x7, 0
    fdiv.s   f0, f0, f1
    call     :atanf
    flw      f1, x7, 0
    flw      f2, x0, :c_zero
    flt.s    x2, f1, f2
    bne      x2, x0, :atan2_neg_y
    flw      f1, x0, :c_pi_2
    fsub.s   f0, f1, f0
    jal      x0, :atan2_done
:atan2_neg_y
    flw      f1, x0, :c_neg_pi_2
    fsub.s   f0, f1, f0
    jal      x0, :atan2_done

:atan2_zero
    flw      f0, x0, :c_zero
:atan2_done
    addi     x7, x7, 32
    ret

# float acosf(float x)
# return atan2(sqrt(1 - x*x), x), x clamped to [-1, 1]
:acosf
    flw      f1, x0, :c_neg_one
    fmax.s   f0, f0, f1
    flw      f1, x0, :c_one
    fmin.s   f0, f0, f1
    fmv.s    f2, f0
    fmul.s   f1, f0, f0
    flw      f3, x0, :c_one
    fsub.s   f1, f3, f1
    flw      f3, x0, :c_zero
    fmax.s   f1, f1, f3
    fsqrt.s  f0, f1
    fmv.s    f1, f2
    call     :atan2f
    ret

# float log2f(float x)
# exponent + polynomial log2(mantissa)
:log2f
    flw      f1, x0, :c_zero
    flt.s    x2, f0, f1
    bne      x2, x0, :log2_nan
    feq.s    x2, f0, f1
    bne      x2, x0, :log2_zero

    addi     x7, x7, -16
    fsw      x7, f0, 0
    lw       x2, x7, 0
    srliw    x3, x2, 23
    andi     x3, x3, 0xff
    addi     x3, x3, -127       # exponent
    lui      x1, 0x00800000
    addi     x1, x1, -1         # mantissa mask
    and      x2, x2, x1
    lui      x1, 0x3f800000
    or       x2, x2, x1
    sw       x7, x2, 0
    flw      f0, x7, 0          # 1.m
    fcvt.s.w f1, x3             # exponent as float

    flw      f2, x0, :c_one
    fsub.s   f2, f0, f2         # u = m - 1
    flw      f3, x0, :c_two
    fadd.s   f3, f3, f2         # 2 + u
    fdiv.s   f2, f2, f3         # s = u / (2 + u)
    fmul.s   f3, f2, f2         # s*s
    flw      f4, x0, :c_log_15
    flw      f5, x0, :c_log_13
    fmadd.s  f4, f4, f3, f5
    flw      f5, x0, :c_log_11
    fmadd.s  f4, f4, f3, f5
    flw      f5, x0, :c_log_9
    fmadd.s  f4, f4, f3, f5
    flw      f5, x0, :c_log_7
    fmadd.s  f4, f4, f3, f5
    flw      f5, x0, :c_log_5
    fmadd.s  f4, f4, f3, f5
    flw      f5, x0, :c_log_3
    fmadd.s  f4, f4, f3, f5
    flw      f5, x0, :c_one
    fmadd.s  f4, f3, f4, f5     # 1 + s2 * p
    fmul.s   f2, f2, f4         # s * p
    flw      f5, x0, :c_log2_scale
    fmul.s   f2, f2, f5
    fadd.s   f0, f1, f2
    addi     x7, x7, 16
    ret
:log2_zero
    flw      f0, x0, :c_neg_inf
    ret
:log2_nan
    flw      f0, x0, :c_nan
    ret

# float logf(float x)
# return log2(x) * ln(2)
:logf
    call     :log2f
    flw      f1, x0, :c_ln2
    fmul.s   f0, f0, f1
    ret

# float exp2f(float x)
# fractional polynomial + exponent built from IEEE bits
:exp2f
    flw      f1, x0, :c_exp2_min
    flt.s    x2, f0, f1
    bne      x2, x0, :exp2_zero
    flw      f1, x0, :c_exp2_max
    fle.s    x2, f1, f0
    bne      x2, x0, :exp2_inf

    addi     x7, x7, -16
    fcvt.w.s x2, f0
    fcvt.s.w f1, x2
    fsub.s   f2, f0, f1         # f = x - trunc(x)
    flw      f3, x0, :c_zero
    flt.s    x3, f2, f3
    beq      x3, x0, :exp2_frac
    addi     x2, x2, -1         # floor for negative x
    flw      f3, x0, :c_one
    fadd.s   f2, f2, f3
:exp2_frac
    flw      f3, x0, :c_exp_7
    flw      f4, x0, :c_exp_6
    fmadd.s  f3, f3, f2, f4
    flw      f4, x0, :c_exp_5
    fmadd.s  f3, f3, f2, f4
    flw      f4, x0, :c_exp_4
    fmadd.s  f3, f3, f2, f4
    flw      f4, x0, :c_exp_3
    fmadd.s  f3, f3, f2, f4
    flw      f4, x0, :c_exp_2
    fmadd.s  f3, f3, f2, f4
    flw      f4, x0, :c_exp_1
    fmadd.s  f3, f3, f2, f4
    flw      f4, x0, :c_exp_0
    fmadd.s  f3, f3, f2, f4

    addi     x2, x2, 127
    slliw    x2, x2, 23
    sw       x7, x2, 0
    flw      f1, x7, 0
    fmul.s   f0, f3, f1
    addi     x7, x7, 16
    ret
:exp2_zero
    flw      f0, x0, :c_zero
    ret
:exp2_inf
    flw      f0, x0, :c_pos_inf
    ret

# float powf(float x, float y)
# return exp2(y * log2(x)); handle 0, inf and nan
:powf
    flw      f2, x0, :c_zero
    feq.s    x2, f0, f2
    beq      x2, x0, :pow_nonzero
    feq.s    x2, f1, f2
    bne      x2, x0, :pow_one
    flt.s    x2, f1, f2
    bne      x2, x0, :pow_inf
    flw      f0, x0, :c_zero
    ret
:pow_one
    flw      f0, x0, :c_one
    ret
:pow_inf
    flw      f0, x0, :c_pos_inf
    ret
:pow_nonzero
    flt.s    x2, f0, f2
    bne      x2, x0, :pow_nan
    addi     x7, x7, -16
    fsw      x7, f1, 0
    call     :log2f
    flw      f1, x7, 0
    fmul.s   f0, f0, f1
    call     :exp2f
    addi     x7, x7, 16
    ret
:pow_nan
    flw      f0, x0, :c_nan
    ret

# ---
# Rotation matrices

# vec3 rot_xy(vec3 p, float a)
# p.xy = [cos a, -sin a; sin a, cos a] * p.xy
:rot_xy
    call     :sincos
    fmul.s   f6, f0, f4         # x*s
    fmul.s   f7, f1, f5         # y*c
    fadd.s   f6, f6, f7         # new y
    fmul.s   f7, f0, f5         # x*c
    fmul.s   f4, f1, f4         # y*s
    fsub.s   f7, f7, f4         # new x
    fmv.s    f0, f7
    fmv.s    f1, f6
    ret

# vec3 rot_yz(vec3 p, float a)
# p.yz = [cos a, -sin a; sin a, cos a] * p.yz
:rot_yz
    call     :sincos
    fmul.s   f6, f1, f4         # y*s
    fmul.s   f7, f2, f5         # z*c
    fadd.s   f6, f6, f7         # new z
    fmul.s   f7, f1, f5         # y*c
    fmul.s   f4, f2, f4         # z*s
    fsub.s   f7, f7, f4         # new y
    fmv.s    f1, f7
    fmv.s    f2, f6
    ret

# float clamp01(float x)
# return clamp(x, 0, 1)
:clamp01
    flw      f1, x0, :c_zero
    fmax.s   f0, f0, f1
    flw      f1, x0, :c_one
    fmin.s   f0, f0, f1
    ret

# uint32_t f2byte(float x)
# return round(clamp(x, 0, 1) * 255)
:f2byte
    flw      f5, x0, :c_zero
    fmax.s   f0, f0, f5
    flw      f5, x0, :c_one
    fmin.s   f0, f0, f5
    flw      f5, x0, :c_byte_max
    fmul.s   f0, f0, f5
    flw      f5, x0, :c_half
    fadd.s   f0, f0, f5
    fcvt.w.s x1, f0
    ret

# ---
# Signed distance fields

# float get_dist(vec3 p)
# p = rot_xy(p, time / 3); p = rot_yz(p, 1)
# return sd_mandelbulb(p, sin(time * 0.2) * 2 + 6)
:get_dist
    # p.xy *= rot(iTime / 3.0)
    flw      f3, x0, :c_time
    flw      f4, x0, :c_time_divisor
    fdiv.s   f3, f3, f4
    call     :rot_xy

    # p.yz *= rot(1.0)
    flw      f3, x0, :c_one
    call     :rot_yz

    # sd_mandelbulb(p, sin(iTime * 0.2) * 2.0 + 6.0)
    flw      f3, x0, :c_time
    flw      f4, x0, :c_time_speed
    fmul.s   f3, f3, f4
    call     :sincos
    fmv.s    f3, f4
    flw      f4, x0, :c_power_amplitude
    fmul.s   f3, f3, f4
    flw      f4, x0, :c_power_bias
    fadd.s   f3, f3, f4
    call     :sd_mandelbulb
    ret

# float sd_mandelbulb(vec3 pos, float power)
# r = length(pos)
# if r > 1.5: return r - 1.2
# repeat MANDELBULB_STEPS:
#   theta = acos(z.y / r); phi = atan2(z.z, z.x)
#   dr = pow(r, power - 1) * power * dr + 1
#   z = pow(r, power) * vec3(sin(theta)*cos(phi), cos(theta), sin(phi)*sin(theta)) + pos
# return 0.5 * log(r) * r / dr
# slots: 0 pos, 3 power, 4 z, 7 r, 8 dr, 9 theta, 10 phi, 11 rp, 12 trig
:sd_mandelbulb
    pushqr   x5
    addi     x7, x7, -128
    fsw      x7, f0, 0
    fsw      x7, f1, 2
    fsw      x7, f2, 4
    fsw      x7, f3, 6
    fsw      x7, f0, 8
    fsw      x7, f1, 10
    fsw      x7, f2, 12

    # r = length(pos)
    flw      f0, x7, 0
    flw      f1, x7, 2
    flw      f2, x7, 4
    call     :v_len
    fsw      x7, f0, 14

    # if (r > 1.5) return r - 1.2;
    flw      f1, x0, :c_bulb_escape_radius
    flt.s    x2, f1, f0
    bne      x2, x0, :sd_early

    # z = pos; dr = 1.0
    flw      f0, x0, :c_one
    fsw      x7, f0, 16
    addi     x5, x0, 0

:sd_loop
    # r = length(z)
    flw      f0, x7, 8
    flw      f1, x7, 10
    flw      f2, x7, 12
    call     :v_len
    fsw      x7, f0, 14

    # if (r > 1.5) break;
    flw      f1, x0, :c_bulb_escape_radius
    flt.s    x2, f1, f0
    bne      x2, x0, :sd_loop_done

    # theta = acos(z.y / r)
    flw      f0, x7, 10
    flw      f1, x7, 14
    fdiv.s   f0, f0, f1
    call     :acosf
    fsw      x7, f0, 18

    # phi = atan(z.z, z.x)
    flw      f0, x7, 12
    flw      f1, x7, 8
    call     :atan2f
    fsw      x7, f0, 20

    # rp = pow(r, power)
    flw      f0, x7, 14
    flw      f1, x7, 6
    call     :powf
    fsw      x7, f0, 22

    # dr = pow(r, power - 1.0) * power * dr + 1.0
    # pow(r, power - 1) is rp / r, saving a second pow.
    flw      f1, x7, 14
    fdiv.s   f0, f0, f1
    flw      f1, x7, 6
    fmul.s   f0, f0, f1
    flw      f1, x7, 16
    fmul.s   f0, f0, f1
    flw      f1, x0, :c_one
    fadd.s   f0, f0, f1
    fsw      x7, f0, 16

    # theta *= power; phi *= power
    flw      f0, x7, 18
    flw      f1, x7, 6
    fmul.s   f0, f0, f1
    fsw      x7, f0, 18
    flw      f0, x7, 20
    fmul.s   f0, f0, f1
    fsw      x7, f0, 20

    # sin/cos of theta and phi
    flw      f3, x7, 18
    call     :sincos
    fsw      x7, f4, 24
    fsw      x7, f5, 26
    flw      f3, x7, 20
    call     :sincos
    fsw      x7, f4, 28
    fsw      x7, f5, 30

    # z = pow(r, power) * vec3(sin(theta) * cos(phi),
    #                          cos(theta),
    #                          sin(phi) * sin(theta)) + pos
    flw      f0, x7, 24         # sin(theta)
    flw      f1, x0, :c_one
    flw      f2, x7, 28         # sin(phi)
    flw      f3, x7, 30         # cos(phi)
    flw      f4, x7, 26         # cos(theta)
    flw      f5, x7, 24         # sin(theta)
    call     :v_mul
    flw      f3, x7, 22         # pow(r, power)
    call     :v_scale
    flw      f3, x7, 0
    flw      f4, x7, 2
    flw      f5, x7, 4
    call     :v_add
    fsw      x7, f0, 8
    fsw      x7, f1, 10
    fsw      x7, f2, 12

    addi     x5, x5, 1
    lwu      x2, x0, :c_mandelbulb_steps
    blt      x5, x2, :sd_loop

:sd_loop_done
    # return 0.5 * log(r) * r / dr
    flw      f0, x7, 14
    call     :logf
    flw      f1, x0, :c_half
    fmul.s   f0, f0, f1
    flw      f1, x7, 14
    fmul.s   f0, f0, f1
    flw      f1, x7, 16
    fdiv.s   f0, f0, f1
    jal      x0, :sd_ret

:sd_early
    flw      f0, x7, 14
    flw      f1, x0, :c_bulb_early_offset
    fsub.s   f0, f0, f1

:sd_ret
    addi     x7, x7, 128
    popqr    x5
    ret

# vec3 get_normal(vec3 p)
# e = 0.001
# d0 = get_dist(p)
# return normalize(vec3(d0) - vec3(get_dist(p - e.x), get_dist(p - e.y), get_dist(p - e.z)))
# slots: 0 p, 3 d0, 4 d
:get_normal
    addi     x7, x7, -64
    fsw      x7, f0, 0
    fsw      x7, f1, 2
    fsw      x7, f2, 4

    # d0 = get_dist(p)
    flw      f0, x7, 0
    flw      f1, x7, 2
    flw      f2, x7, 4
    call     :get_dist
    fsw      x7, f0, 6

    # dx = get_dist(p - e.xyy)
    flw      f0, x7, 0
    flw      f1, x7, 2
    flw      f2, x7, 4
    flw      f3, x0, :c_normal_eps
    fsub.s   f0, f0, f3
    call     :get_dist
    fsw      x7, f0, 8

    # dy = get_dist(p - e.yxy)
    flw      f0, x7, 0
    flw      f1, x7, 2
    flw      f2, x7, 4
    flw      f3, x0, :c_normal_eps
    fsub.s   f1, f1, f3
    call     :get_dist
    fsw      x7, f0, 10

    # dz = get_dist(p - e.yyx)
    flw      f0, x7, 0
    flw      f1, x7, 2
    flw      f2, x7, 4
    flw      f3, x0, :c_normal_eps
    fsub.s   f2, f2, f3
    call     :get_dist
    fsw      x7, f0, 12

    # normalize(d0 - vec3(dx, dy, dz))
    flw      f0, x7, 6
    fmv.s    f1, f0
    fmv.s    f2, f0
    flw      f3, x7, 8
    flw      f4, x7, 10
    flw      f5, x7, 12
    call     :v_sub
    call     :v_norm

    addi     x7, x7, 64
    ret

# ---
# Ray marching

# float march_ray(vec3 ro, vec3 rd)
# for MAX_RAY_STEPS:
#   p = ro + dO * rd; dS = get_dist(p); dO += dS
#   if dS < 0.002: return dO
#   if dO > 15: return -1
# slots: 0 ro, 3 rd, 6 dO, 7 dS
:march_ray
    pushqr   x5
    addi     x7, x7, -80
    fsw      x7, f0, 0
    fsw      x7, f1, 2
    fsw      x7, f2, 4
    fsw      x7, f3, 6
    fsw      x7, f4, 8
    fsw      x7, f5, 10

    flw      f0, x0, :c_zero
    fsw      x7, f0, 12
    addi     x5, x0, 0

:march_loop
    # p = ro + dO * rd
    flw      f0, x7, 6
    flw      f1, x7, 8
    flw      f2, x7, 10
    flw      f3, x7, 12
    call     :v_scale
    flw      f3, x7, 0
    flw      f4, x7, 2
    flw      f5, x7, 4
    call     :v_add

    call     :get_dist
    fsw      x7, f0, 14

    # dO += dS
    flw      f1, x7, 12
    fadd.s   f1, f1, f0
    fsw      x7, f1, 12

    # if (dS < HIT_DIST) return dO;
    flw      f2, x0, :c_hit_dist
    flt.s    x2, f0, f2
    bne      x2, x0, :march_hit

    # if (dO > MAX_DIST) return -1.0;
    flw      f2, x0, :c_max_dist
    flt.s    x2, f2, f1
    bne      x2, x0, :march_miss

    addi     x5, x5, 1
    lwu      x2, x0, :c_max_ray_steps
    blt      x5, x2, :march_loop

    flw      f0, x7, 12
    jal      x0, :march_ret

:march_hit
    flw      f0, x7, 12
    jal      x0, :march_ret

:march_miss
    flw      f0, x0, :c_neg_one

:march_ret
    addi     x7, x7, 80
    popqr    x5
    ret

# ---
# Ray marching based lighting

# float ambient_occlusion(vec3 p, vec3 n)
# for AMBIENT_OCC_STEPS:
#   hr = 0.02 + 0.025 * i*i
#   d = get_dist(p + n * hr)
#   occ += -(d - hr) * sca; sca *= 0.95
# return 1 - clamp(occ, 0, 1)
# slots: 0 p, 3 n, 6 occ, 7 sca, 8 hr
:ambient_occlusion
    pushqr   x5
    addi     x7, x7, -80
    fsw      x7, f0, 0
    fsw      x7, f1, 2
    fsw      x7, f2, 4
    fsw      x7, f3, 6
    fsw      x7, f4, 8
    fsw      x7, f5, 10

    flw      f0, x0, :c_zero
    fsw      x7, f0, 12
    flw      f0, x0, :c_one
    fsw      x7, f0, 14
    addi     x5, x0, 0

:ao_loop
    # hr = 0.02 + 0.025 * float(i * i)
    mulw     x2, x5, x5
    fcvt.s.w f6, x2
    flw      f7, x0, :c_ao_scale
    fmul.s   f6, f6, f7
    flw      f7, x0, :c_ao_floor
    fadd.s   f6, f6, f7
    fsw      x7, f6, 16

    # aop = n * hr + p
    flw      f0, x7, 6
    flw      f1, x7, 8
    flw      f2, x7, 10
    flw      f3, x7, 16
    call     :v_scale
    flw      f3, x7, 0
    flw      f4, x7, 2
    flw      f5, x7, 4
    call     :v_add

    call     :get_dist

    # occ += -(d - hr) * sca
    flw      f6, x7, 16
    fsub.s   f0, f0, f6
    fneg.s   f0, f0
    flw      f1, x7, 14
    fmul.s   f0, f0, f1
    flw      f2, x7, 12
    fadd.s   f2, f2, f0
    fsw      x7, f2, 12

    # sca *= 0.95
    flw      f0, x0, :c_ao_falloff
    fmul.s   f1, f1, f0
    fsw      x7, f1, 14

    addi     x5, x5, 1
    lwu      x2, x0, :c_ambient_occ_steps
    blt      x5, x2, :ao_loop

    # return 1.0 - clamp(occ, 0.0, 1.0)
    flw      f0, x7, 12
    call     :clamp01
    flw      f1, x0, :c_one
    fsub.s   f0, f1, f0

    addi     x7, x7, 80
    popqr    x5
    ret

# vec3 shade(float u, float v)
# ro = (-3, 0, 0); rd = normalize(vec3(1, u, v))
# d = march_ray(ro, rd)
# hit: normal, AO, sun/sky diffuse, object color; miss: sky
# slots: 0 uv, 2 ro, 5 rd, 8 d, 9 p, 12 n, 15 occ, 16 dif, 18 t, 19 obj, 24 col
:shade
    addi     x7, x7, -224
    fsw      x7, f0, 0
    fsw      x7, f1, 2

    # ro = vec3(-3.0, 0.0, 0.0)
    flw      f0, x0, :c_camera_x
    fsw      x7, f0, 4
    flw      f0, x0, :c_zero
    fsw      x7, f0, 6
    fsw      x7, f0, 8

    # rd = normalize(vec3(1.0, uv))
    flw      f0, x0, :c_one
    flw      f1, x7, 0
    flw      f2, x7, 2
    call     :v_norm
    fsw      x7, f0, 10
    fsw      x7, f1, 12
    fsw      x7, f2, 14

    # d = march_ray(ro, rd)
    flw      f0, x7, 4
    flw      f1, x7, 6
    flw      f2, x7, 8
    flw      f3, x7, 10
    flw      f4, x7, 12
    flw      f5, x7, 14
    call     :march_ray
    fsw      x7, f0, 16

    # if (d == -1.0) sky
    flw      f1, x0, :c_neg_one
    feq.s    x2, f0, f1
    bne      x2, x0, :shade_sky

    # p = ro + rd * d
    flw      f6, x7, 16
    flw      f0, x7, 4
    flw      f1, x7, 6
    flw      f2, x7, 8
    flw      f3, x7, 10
    flw      f4, x7, 12
    flw      f5, x7, 14
    fmul.s   f3, f3, f6
    fmul.s   f4, f4, f6
    fmul.s   f5, f5, f6
    fadd.s   f0, f0, f3
    fadd.s   f1, f1, f4
    fadd.s   f2, f2, f5
    fsw      x7, f0, 18
    fsw      x7, f1, 20
    fsw      x7, f2, 22

    # n = get_normal(p)
    flw      f0, x7, 18
    flw      f1, x7, 20
    flw      f2, x7, 22
    call     :get_normal
    fsw      x7, f0, 24
    fsw      x7, f1, 26
    fsw      x7, f2, 28

    # occ = ambient_occlusion(p, n)
    flw      f0, x7, 18
    flw      f1, x7, 20
    flw      f2, x7, 22
    flw      f3, x7, 24
    flw      f4, x7, 26
    flw      f5, x7, 28
    call     :ambient_occlusion
    fsw      x7, f0, 30

    # sunDif = max(0, dot(n, sunDir))
    flw      f0, x7, 24
    flw      f1, x7, 26
    flw      f2, x7, 28
    flw      f3, x0, :c_sun_x
    flw      f4, x0, :c_sun_y
    flw      f5, x0, :c_sun_z
    call     :v_dot
    flw      f1, x0, :c_zero
    fmax.s   f0, f0, f1
    fsw      x7, f0, 32

    # skyDif = max(0, n.z)
    flw      f0, x7, 28
    flw      f1, x0, :c_zero
    fmax.s   f0, f0, f1
    fsw      x7, f0, 34

    # t = clamp(length(p) - 0.43, 0, 1)
    flw      f0, x7, 18
    flw      f1, x7, 20
    flw      f2, x7, 22
    call     :v_len
    flw      f1, x0, :c_obj_gradient_offset
    fsub.s   f0, f0, f1
    call     :clamp01
    fsw      x7, f0, 36

    # objColor = mix(vec3(0.2, 0.7, 1.0),
    #                vec3(1.0, 0.52, 0.15), t)
    flw      f0, x0, :c_obj_a_r
    flw      f1, x0, :c_obj_a_g
    flw      f2, x0, :c_obj_a_b
    flw      f3, x0, :c_obj_b_r
    flw      f4, x0, :c_obj_b_g
    flw      f5, x0, :c_obj_b_b
    flw      f6, x7, 36
    call     :mix3
    fsw      x7, f0, 38
    fsw      x7, f1, 40
    fsw      x7, f2, 42

    # dif1 = sunDif * 0.8 + 0.2
    flw      f0, x7, 32
    flw      f1, x0, :c_sun_dif_scale
    fmul.s   f0, f0, f1
    flw      f1, x0, :c_sun_dif_bias
    fadd.s   f0, f0, f1
    fsw      x7, f0, 44

    # dif2 = skyDif * 0.6 + 0.1
    flw      f0, x7, 34
    flw      f1, x0, :c_sky_dif_scale
    fmul.s   f0, f0, f1
    flw      f1, x0, :c_sky_dif_bias
    fadd.s   f0, f0, f1
    fsw      x7, f0, 46

    # col.r = (sunColor.r * dif1 + skyColor.r * dif2) * occ * obj.r
    flw      f0, x7, 44
    flw      f1, x0, :c_sun_col_r
    fmul.s   f1, f1, f0
    flw      f0, x7, 46
    flw      f2, x0, :c_sky_col_r
    fmul.s   f2, f2, f0
    fadd.s   f1, f1, f2
    flw      f0, x7, 30
    fmul.s   f1, f1, f0
    flw      f0, x7, 38
    fmul.s   f1, f1, f0
    fsw      x7, f1, 48

    # col.g
    flw      f0, x7, 44
    flw      f1, x0, :c_sun_col_g
    fmul.s   f1, f1, f0
    flw      f0, x7, 46
    flw      f2, x0, :c_sky_col_g
    fmul.s   f2, f2, f0
    fadd.s   f1, f1, f2
    flw      f0, x7, 30
    fmul.s   f1, f1, f0
    flw      f0, x7, 40
    fmul.s   f1, f1, f0
    fsw      x7, f1, 50

    # col.b
    flw      f0, x7, 44
    flw      f1, x0, :c_sun_col_b
    fmul.s   f1, f1, f0
    flw      f0, x7, 46
    flw      f2, x0, :c_sky_col_b
    fmul.s   f2, f2, f0
    fadd.s   f1, f1, f2
    flw      f0, x7, 30
    fmul.s   f1, f1, f0
    flw      f0, x7, 42
    fmul.s   f1, f1, f0
    fsw      x7, f1, 52

    jal      x0, :shade_ret

:shade_sky
    flw      f0, x0, :c_sky_r
    flw      f1, x0, :c_sky_g
    flw      f2, x0, :c_sky_b
    fsw      x7, f0, 48
    fsw      x7, f1, 50
    fsw      x7, f2, 52

:shade_ret
    flw      f0, x7, 48
    flw      f1, x7, 50
    flw      f2, x7, 52
    addi     x7, x7, 224
    ret

# ---
# Entry point

# void _start(void)
# validate config, read framebuffer
# for each pixel: uv = (x + 0.5 - half_w, half_h - y - 0.5) / height
# color = shade(uv), store RGBA, flush forever
# slots: 0 fb_addr, 1 stride, 2 width, 3 height, 4 x, 5 y, 6 half_w, 7 half_h, 8 inv_h
:_start
    addi     x7, x7, -128

    # Validate the boot config.
    lw       x3, x1, 0
    lui      x6, 0x57535000
    addi     x6, x6, 0x446
    bne      x3, x6, :bad_config
    lw       x3, x1, 1
    addi     x6, x0, 1
    bne      x3, x6, :bad_config

    # Read the framebuffer.
    ld       x2, x1, 2
    lwu      x5, x1, 6
    lwu      x3, x1, 7
    beq      x3, x0, :no_fb
    addi     x6, x0, 1
    bne      x3, x6, :bad_format
    st       x7, x2, 0
    st       x7, x5, 1

    lwu      x3, x1, 2
    st       x7, x3, 2
    lwu      x3, x1, 3
    st       x7, x3, 3

    # half_w = width * 0.5
    ld       x3, x7, 2
    fcvt.s.w f0, x3
    flw      f1, x0, :c_half
    fmul.s   f0, f0, f1
    fsw      x7, f0, 12

    # half_h = height * 0.5
    ld       x3, x7, 3
    fcvt.s.w f0, x3
    flw      f1, x0, :c_half
    fmul.s   f0, f0, f1
    fsw      x7, f0, 14

    # inv_h = 1.0 / height
    ld       x3, x7, 3
    fcvt.s.w f0, x3
    flw      f1, x0, :c_one
    fdiv.s   f0, f1, f0
    fsw      x7, f0, 16

    st       x7, x0, 4
    st       x7, x0, 5

:y_loop
    ld       x2, x7, 5
    ld       x3, x7, 3
    bgeu     x2, x3, :render_done
    st       x7, x0, 4

:x_loop
    ld       x2, x7, 4
    ld       x3, x7, 2
    bgeu     x2, x3, :next_row

    # uv.x = (x + 0.5 - half_w) / height
    fcvt.s.w f0, x2
    flw      f2, x0, :c_half
    fadd.s   f0, f0, f2
    flw      f2, x7, 12
    fsub.s   f0, f0, f2
    flw      f2, x7, 16
    fmul.s   f0, f0, f2

    # uv.y = (half_h - (y + 0.5)) / height, framebuffer y is top down
    ld       x2, x7, 5
    fcvt.s.w f1, x2
    flw      f2, x0, :c_half
    fadd.s   f1, f1, f2
    flw      f2, x7, 14
    fsub.s   f1, f2, f1
    flw      f2, x7, 16
    fmul.s   f1, f1, f2

    call     :shade

    # Convert the color to RGBA8888.
    fmv.s    f3, f1
    fmv.s    f4, f2
    call     :f2byte
    addi     x6, x1, 0
    fmv.s    f0, f3
    call     :f2byte
    slliw    x1, x1, 8
    or       x6, x6, x1
    fmv.s    f0, f4
    call     :f2byte
    slliw    x1, x1, 16
    or       x6, x6, x1
    lui      x1, 0xff000000
    or       x6, x6, x1

    # Pixel address = fb_addr + y * stride + x * 4
    ld       x2, x7, 5
    ld       x4, x7, 1
    mul      x2, x2, x4
    ld       x4, x7, 0
    add      x2, x2, x4
    ld       x4, x7, 4
    slli     x4, x4, 2
    add      x2, x2, x4
    sw       x2, x6, 0

    # x += 1
    ld       x3, x7, 4
    addi     x3, x3, 1
    st       x7, x3, 4
    jal      x0, :x_loop

:next_row
    ld       x3, x7, 5
    addi     x3, x3, 1
    st       x7, x3, 5
    jal      x0, :y_loop

:render_done
    addi     x1, x0, 2
    ecall
    jal      x0, :render_done

:bad_config
    addi     x1, x0, :msg_bad_config
    call     :print
    addi     x2, x0, 3
    jal      x0, :exit

:no_fb
    addi     x1, x0, :msg_no_fb
    call     :print
    addi     x2, x0, 1
    jal      x0, :exit

:bad_format
    addi     x1, x0, :msg_bad_format
    call     :print
    addi     x2, x0, 1
    jal      x0, :exit

section .data
    c_zero:                f32 0.0
    c_half:                f32 0.5
    c_one:                 f32 1.0
    c_two:                 f32 2.0
    c_time_divisor:        f32 3.0
    c_power_amplitude:     f32 2.0
    c_power_bias:          f32 6.0
    c_byte_max:            f32 255.0
    c_neg_one:             f32 -1.0
    c_camera_x:            f32 -3.0
    c_sky_dif_bias:        f32 0.1
    c_time_speed:          f32 0.2
    c_sun_dif_bias:        f32 0.2
    c_ao_floor:            f32 0.02
    c_ao_scale:            f32 0.025
    c_obj_gradient_offset: f32 0.43
    c_sky_dif_scale:       f32 0.6
    c_sun_dif_scale:       f32 0.8
    c_ao_falloff:          f32 0.95
    c_bulb_early_offset:   f32 1.2
    c_bulb_escape_radius:  f32 1.5
    c_normal_eps:          f32 0.001
    c_hit_dist:            f32 0.002
    c_max_dist:            f32 15.0
    c_time:                f32 0.0

    c_mandelbulb_steps:    u32 15
    c_max_ray_steps:       u32 256
    c_ambient_occ_steps:   u32 5

    c_sun_x:               f32 -0.27216553
    c_sun_y:               f32 -0.68041384
    c_sun_z:               f32 0.68041384

    c_sun_col_r:           f32 2.8
    c_sun_col_g:           f32 2.3
    c_sun_col_b:           f32 1.75
    c_sky_col_r:           f32 0.0
    c_sky_col_g:           f32 1.0
    c_sky_col_b:           f32 1.8

    c_obj_a_r:             f32 0.2
    c_obj_a_g:             f32 0.7
    c_obj_a_b:             f32 1.0
    c_obj_b_r:             f32 1.0
    c_obj_b_g:             f32 0.52
    c_obj_b_b:             f32 0.15

    c_sky_r:               f32 0.4
    c_sky_g:               f32 0.7
    c_sky_b:               f32 0.9

    # Software math constants.
    c_2_over_pi:           f32 0.63661977
    c_pi:                  f32 3.1415927
    c_neg_pi:              f32 -3.1415927
    c_pi_2:                f32 1.5707964
    c_neg_pi_2:            f32 -1.5707964
    c_ln2:                 f32 0.69314718
    c_log2_scale:          f32 2.8853900
    c_exp2_min:            f32 -126.0
    c_exp2_max:            f32 128.0
    c_pos_inf:             f32 inf
    c_neg_inf:             f32 -inf
    c_nan:                 f32 nan

    c_sin_1:               f32 1.0
    c_sin_3:               f32 -0.16666667
    c_sin_5:               f32 0.0083333338
    c_sin_7:               f32 -0.00019841270
    c_sin_9:               f32 0.0000027557319
    c_sin_11:              f32 -0.000000025052108

    c_cos_0:               f32 1.0
    c_cos_2:               f32 -0.5
    c_cos_4:               f32 0.041666668
    c_cos_6:               f32 -0.0013888889
    c_cos_8:               f32 0.000024801588
    c_cos_10:              f32 -0.00000027557319
    c_cos_12:              f32 0.0000000020876757

    c_atan_0:              f32 0.99999992
    c_atan_1:              f32 -0.33332233
    c_atan_2:              f32 0.19974029
    c_atan_3:              f32 -0.14047823
    c_atan_4:              f32 0.10002211
    c_atan_5:              f32 -0.06087457
    c_atan_6:              f32 0.02533178
    c_atan_7:              f32 -0.00502109

    c_log_3:               f32 0.33333334
    c_log_5:               f32 0.2
    c_log_7:               f32 0.14285715
    c_log_9:               f32 0.11111111
    c_log_11:              f32 0.09090909
    c_log_13:              f32 0.07692308
    c_log_15:              f32 0.06666667

    c_exp_0:               f32 1.0
    c_exp_1:               f32 0.69314718
    c_exp_2:               f32 0.24022633
    c_exp_3:               f32 0.055505402
    c_exp_4:               f32 0.0096133364
    c_exp_5:               f32 0.0013431473
    c_exp_6:               f32 0.00014294692
    c_exp_7:               f32 0.000021647296

    msg_bad_config_len:    u64 23
    msg_bad_config:        str "mandelbulb: bad config\n"
    msg_no_fb_len:         u64 27
    msg_no_fb:             str "mandelbulb: no framebuffer\n"
    msg_bad_format_len:    u64 26
    msg_bad_format:        str "mandelbulb: bad fb format\n"
