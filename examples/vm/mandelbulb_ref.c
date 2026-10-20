#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define I_TIME 0.0f
#define MAX_DIST 15.0f
#define HIT_DIST 0.002f
#define MAX_RAY_STEPS 256
#define AMBIENT_OCC_STEPS 5
#define MANDELBULB_STEPS 15

#define BULB_ESCAPE_RADIUS 1.5f
#define BULB_EARLY_OFFSET 1.2f
#define NORMAL_EPS 0.001f
#define AO_FLOOR 0.02f
#define AO_SCALE 0.025f
#define AO_FALLOFF 0.95f
#define OBJ_GRADIENT_OFFSET 0.43f
#define SUN_DIF_SCALE 0.8f
#define SUN_DIF_BIAS 0.2f
#define SKY_DIF_SCALE 0.6f
#define SKY_DIF_BIAS 0.1f
#define TIME_DIVISOR 3.0f
#define TIME_SPEED 0.2f
#define POWER_AMPLITUDE 2.0f
#define POWER_BIAS 6.0f

typedef struct {
    float x, y, z;
} vec3;

static vec3 v3(float x, float y, float z)
{
    vec3 v = { x, y, z };
    return v;
}

static vec3 v_add(vec3 a, vec3 b)
{
    return v3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static vec3 v_sub(vec3 a, vec3 b)
{
    return v3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static vec3 v_scale(vec3 a, float s)
{
    return v3(a.x * s, a.y * s, a.z * s);
}

static vec3 v_mul(vec3 a, vec3 b)
{
    return v3(a.x * b.x, a.y * b.y, a.z * b.z);
}

static float v_dot(vec3 a, vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float v_len(vec3 a)
{
    return sqrtf(v_dot(a, a));
}

static vec3 v_norm(vec3 a)
{
    return v_scale(a, 1.0f / v_len(a));
}

static vec3 mix3(vec3 a, vec3 b, float t)
{
    return v_add(a, v_scale(v_sub(b, a), t));
}

static float clamp01(float x)
{
    return fmaxf(0.0f, fminf(1.0f, x));
}

static vec3 rot_xy(vec3 p, float a)
{
    float s = sinf(a);
    float c = cosf(a);
    float x = p.x;
    float y = p.y;

    p.x = x * c - y * s;
    p.y = x * s + y * c;
    return p;
}

static vec3 rot_yz(vec3 p, float a)
{
    float s = sinf(a);
    float c = cosf(a);
    float y = p.y;
    float z = p.z;

    p.y = y * c - z * s;
    p.z = y * s + z * c;
    return p;
}

static float sd_mandelbulb(vec3 pos, float power)
{
    float r = v_len(pos);
    if (r > BULB_ESCAPE_RADIUS)
        return r - BULB_EARLY_OFFSET;

    vec3 z = pos;
    float dr = 1.0f;

    for (int i = 0; i < MANDELBULB_STEPS; i++) {
        r = v_len(z);
        if (r > BULB_ESCAPE_RADIUS)
            break;

        float theta = acosf(z.y / r) * power;
        float phi = atan2f(z.z, z.x) * power;
        dr = powf(r, power - 1.0f) * power * dr + 1.0f;
        z = v_add(v_scale(v3(sinf(theta) * cosf(phi),
                            cosf(theta),
                            sinf(phi) * sinf(theta)),
                            powf(r, power)), pos);
    }

    return 0.5f * logf(r) * r / dr;
}

static float get_dist(vec3 p)
{
    p = rot_xy(p, I_TIME / TIME_DIVISOR);
    p = rot_yz(p, 1.0f);
    return sd_mandelbulb(p, sinf(I_TIME * TIME_SPEED) * POWER_AMPLITUDE + POWER_BIAS);
}

static vec3 get_normal(vec3 p)
{
    const float e = NORMAL_EPS;
    float d0 = get_dist(p);
    vec3 d = v3(get_dist(v_sub(p, v3(e, 0.0f, 0.0f))),
                get_dist(v_sub(p, v3(0.0f, e, 0.0f))),
                get_dist(v_sub(p, v3(0.0f, 0.0f, e))));

    return v_norm(v_sub(v3(d0, d0, d0), d));
}

static float march_ray(vec3 ro, vec3 rd)
{
    float dO = 0.0f;

    for (int i = 0; i < MAX_RAY_STEPS; i++) {
        vec3 p = v_add(ro, v_scale(rd, dO));
        float dS = get_dist(p);
        dO += dS;
        if (dS < HIT_DIST)
            return dO;
        if (dO > MAX_DIST)
            return -1.0f;
    }

    return dO;
}

static float ambient_occlusion(vec3 p, vec3 n)
{
    float occ = 0.0f;
    float sca = 1.0f;

    for (int i = 0; i < AMBIENT_OCC_STEPS; i++) {
        float hr = AO_FLOOR + AO_SCALE * (float)(i * i);
        float d = get_dist(v_add(p, v_scale(n, hr)));
        occ += -(d - hr) * sca;
        sca *= AO_FALLOFF;
    }

    return 1.0f - clamp01(occ);
}

static vec3 shade(float u, float v)
{
    vec3 ro = v3(-3.0f, 0.0f, 0.0f);
    vec3 rd = v_norm(v3(1.0f, u, v));
    vec3 sun_dir = v_norm(v3(-0.2f, -0.5f, 0.5f));
    vec3 sun_color = v3(2.8f, 2.3f, 1.75f);
    vec3 sky_color = v3(0.0f, 1.0f, 1.8f);
    float d = march_ray(ro, rd);

    if (d < 0.0f)
        return v3(0.4f, 0.7f, 0.9f);

    vec3 p = v_add(ro, v_scale(rd, d));
    vec3 n = get_normal(p);
    float occ = ambient_occlusion(p, n);
    float sun_dif = fmaxf(0.0f, v_dot(n, sun_dir));
    float sky_dif = fmaxf(0.0f, n.z);
    vec3 obj_color = mix3(v3(0.2f, 0.7f, 1.0f),
                          v3(1.0f, 0.52f, 0.15f),
                          clamp01(v_len(p) - OBJ_GRADIENT_OFFSET));
    float dif1 = sun_dif * SUN_DIF_SCALE + SUN_DIF_BIAS;
    float dif2 = sky_dif * SKY_DIF_SCALE + SKY_DIF_BIAS;

    vec3 light = v_add(v_scale(sun_color, dif1), v_scale(sky_color, dif2));
    return v_mul(v_scale(light, occ), obj_color);
}

int main(int argc, char **argv)
{
    int w = argc > 1 ? atoi(argv[1]) : 160;
    int h = argc > 2 ? atoi(argv[2]) : 120;
    const char *path = argc > 3 ? argv[3] : "mandelbulb.ppm";

    if (w <= 0 || h <= 0) {
        fprintf(stderr, "usage: %s [width] [height] [output.ppm]\n", argv[0]);
        return 1;
    }

    unsigned char *img = malloc((size_t)w * (size_t)h * 3u);
    if (img == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    clock_t start = clock();
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float u = ((float)x + 0.5f - 0.5f * (float)w) / (float)h;
            float v = (0.5f * (float)h - ((float)y + 0.5f)) / (float)h;
            vec3 c = shade(u, v);
            unsigned char *p = img + ((size_t)y * (size_t)w + (size_t)x) * 3u;

            p[0] = (unsigned char)(clamp01(c.x) * 255.0f + 0.5f);
            p[1] = (unsigned char)(clamp01(c.y) * 255.0f + 0.5f);
            p[2] = (unsigned char)(clamp01(c.z) * 255.0f + 0.5f);
        }
    }

    double secs = (double)(clock() - start) / CLOCKS_PER_SEC;
    fprintf(stderr, "%dx%d in %.3f s\n", w, h, secs);

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        free(img);
        return 1;
    }

    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(img, 1, (size_t)w * (size_t)h * 3u, f);
    fclose(f);
    free(img);
    return 0;
}
