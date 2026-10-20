#define MAX_DIST 15.0
#define HIT_DIST 0.002
#define MAX_RAY_STEPS 256
#define SOFT_SHADOW_STEPS 32
#define AMBIENT_OCC_STEPS 5
#define MANDELBULB_STEPS 15

mat2 rot(float a)
{
    float s = sin(a);
    float c = cos(a);
    return mat2(c, -s, s, c);
}

float sd_mandelbulb(vec3 pos, float power)
{
    float r = length(pos);
    if (r > 1.5) return r - 1.2;

    vec3 z = pos;
	float dr = 1.0;
    float theta;
    float phi;

	for (int i = 0; i < MANDELBULB_STEPS; i++) {
		r = length(z);
		if (r > 1.5)
            break;
		theta = acos(z.y / r);
		phi = atan(z.z, z.x);
		dr = pow(r, power - 1.0) * power * dr + 1.0;
		theta *= power;
		phi *= power;
		z = pow(r, power) * vec3(
            sin(theta) * cos(phi),
            cos(theta),
            sin(phi) * sin(theta)
        ) + pos;
	}

	return 0.5 * log(r) * r / dr;
}

float get_dist(vec3 p) {
    p.xy *= rot(iTime / 3.0);
    p.yz *= rot(1.0);
    float mandelbulb = sd_mandelbulb(p, sin(iTime * 0.2) * 2.0 + 6.0);
    return mandelbulb;
}

vec3 get_normal(vec3 p) {
    vec2 e = vec2(0.001, 0);
    return normalize(get_dist(p) - vec3(
        get_dist(p - e.xyy),
        get_dist(p - e.yxy),
        get_dist(p - e.yyx))
    );
}

float march_ray(vec3 ro, vec3 rd) {
    float dO = 0.0;

    for (int i = 0; i < MAX_RAY_STEPS; i++) {
        vec3 p = ro + dO * rd;
        float dS = get_dist(p);
        dO += dS;
        if (dS < HIT_DIST) return dO;
        if (dO > MAX_DIST) return -1.0;
    }
}

float ambient_occlusion(vec3 p, vec3 n) {
    float occ = 0.0;
    float sca = 1.0;

    for (int i = 0; i < AMBIENT_OCC_STEPS; i++) {
        float hr = 0.02 + 0.025 * float(i * i);
        vec3 aop = n * hr + p;
        float d = get_dist(aop);
        occ += -(d - hr) * sca;
        sca *= 0.95;
    }
    return 1.0 - clamp(occ, 0.0, 1.0);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord.xy - 0.5 * iResolution.xy) / iResolution.y;

    vec3 ro = vec3(-3.0, 0.0, 0.0);
    vec3 rd = normalize(vec3(1.0, uv));

    vec3 col = vec3(0.0);

    vec3 sunDir = normalize(vec3(-0.2, -0.5, 0.5));
    vec3 sunColor = vec3(2.8, 2.3, 1.75);
    vec3 skyColor = vec3(0.0, 1.0, 1.8);

    float d = march_ray(ro, rd);

    if (d != -1.0) {
        vec3 p = ro + rd * d;
        vec3 n = get_normal(p);
        float occ = ambient_occlusion(p, n);
        float sunDif = max(0.0, dot(n, sunDir));
        float skyDif = max(0.0, dot(n, vec3(0.0, 0.0, 1.0)));
        vec3 objColor = mix(vec3(0.2, 0.7, 1.0), vec3(1.0, 0.52, 0.15), clamp(length(p)-0.43, 0.0, 1.0));
        col = (sunColor * (sunDif * 0.8 + 0.2) + skyColor * (skyDif * 0.6 + 0.1))
             * occ * objColor;
    }
    else
        col = vec3(0.4, 0.7, 0.9);
    fragColor = vec4(col, 1.0);
}
