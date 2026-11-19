#include <gpu3d.h>
#include <mm/heap.h>
#include <string.h>

static float*   g_zbuf = NULL;
static uint32_t g_zbuf_cap = 0;

static float fast_sin(float x) {
    while (x > 3.14159265f) x -= 6.2831853f;
    while (x < -3.14159265f) x += 6.2831853f;
    float x2 = x * x;
    return x * (1.0f - x2 / 6.0f + (x2 * x2) / 120.0f - (x2 * x2 * x2) / 5040.0f);
}

static float fast_cos(float x) {
    return fast_sin(x + 1.5707963f);
}

static uint32_t lerp_color(uint32_t c0, uint32_t c1, uint32_t c2, float w0, float w1, float w2) {
    uint32_t r = (uint32_t)(((c0 >> 16) & 0xFF) * w0 + ((c1 >> 16) & 0xFF) * w1 + ((c2 >> 16) & 0xFF) * w2);
    uint32_t g = (uint32_t)(((c0 >> 8) & 0xFF) * w0 + ((c1 >> 8) & 0xFF) * w1 + ((c2 >> 8) & 0xFF) * w2);
    uint32_t b = (uint32_t)((c0 & 0xFF) * w0 + (c1 & 0xFF) * w1 + (c2 & 0xFF) * w2);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return 0xFF000000U | (r << 16) | (g << 8) | b;
}

bool soft3d_clear(uint32_t* color_buf, uint32_t w, uint32_t h, uint32_t argb) {
    if (!color_buf || w == 0 || h == 0) return false;
    uint32_t total = w * h;
    if (total > g_zbuf_cap) {
        if (g_zbuf) kfree(g_zbuf);
        g_zbuf = (float*)kmalloc(total * sizeof(float));
        g_zbuf_cap = g_zbuf ? total : 0;
    }
    for (uint32_t i = 0; i < total; i++) {
        color_buf[i] = argb;
        if (g_zbuf) g_zbuf[i] = 1e9f;
    }
    return true;
}

bool soft3d_draw_mesh(uint32_t* color_buf, uint32_t w, uint32_t h,
                      const gpu3d_vertex_t* verts, uint32_t tri_count,
                      float angle_x, float angle_y) {
    if (!color_buf || !verts || w == 0 || h == 0 || tri_count == 0) return false;
    if (w * h > g_zbuf_cap || !g_zbuf) {
        soft3d_clear(color_buf, w, h, 0xFF101824U);
    }

    float sx = fast_sin(angle_x), cx = fast_cos(angle_x);
    float sy = fast_sin(angle_y), cy = fast_cos(angle_y);
    float half_w = (float)w * 0.5f;
    float half_h = (float)h * 0.5f;
    float fov = half_h * 1.6f;

    for (uint32_t t = 0; t < tri_count; t++) {
        float px[3], py[3], pz[3];
        uint32_t vc[3];
        bool behind = false;

        for (int k = 0; k < 3; k++) {
            const gpu3d_vertex_t* v = &verts[t * 3 + k];
            vc[k] = v->color;
            // Rotazione Y poi X
            float rx = v->x * cy + v->z * sy;
            float rz1 = -v->x * sy + v->z * cy;
            float ry = v->y * cx - rz1 * sx;
            float rz = v->y * sx + rz1 * cx + 3.2f;
            if (rz <= 0.2f) {
                behind = true;
                break;
            }
            px[k] = half_w + (rx * fov) / rz;
            py[k] = half_h - (ry * fov) / rz;
            pz[k] = rz;
        }
        if (behind) continue;

        float area = (px[1] - px[0]) * (py[2] - py[0]) - (py[1] - py[0]) * (px[2] - px[0]);
        if (area <= 0.01f) continue; // Back-face culling
        float inv_area = 1.0f / area;

        int32_t minx = (int32_t)px[0], maxx = minx;
        int32_t miny = (int32_t)py[0], maxy = miny;
        for (int k = 1; k < 3; k++) {
            if ((int32_t)px[k] < minx) minx = (int32_t)px[k];
            if ((int32_t)px[k] > maxx) maxx = (int32_t)px[k];
            if ((int32_t)py[k] < miny) miny = (int32_t)py[k];
            if ((int32_t)py[k] > maxy) maxy = (int32_t)py[k];
        }
        if (minx < 0) minx = 0;
        if (miny < 0) miny = 0;
        if (maxx >= (int32_t)w) maxx = (int32_t)w - 1;
        if (maxy >= (int32_t)h) maxy = (int32_t)h - 1;

        for (int32_t y = miny; y <= maxy; y++) {
            float fy = (float)y + 0.5f;
            for (int32_t x = minx; x <= maxx; x++) {
                float fx = (float)x + 0.5f;
                float w0 = ((px[2] - px[1]) * (fy - py[1]) - (py[2] - py[1]) * (fx - px[1])) * inv_area;
                float w1 = ((px[0] - px[2]) * (fy - py[2]) - (py[0] - py[2]) * (fx - px[2])) * inv_area;
                float w2 = 1.0f - w0 - w1;
                if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                    float z = pz[0] * w0 + pz[1] * w1 + pz[2] * w2;
                    uint32_t idx = (uint32_t)y * w + (uint32_t)x;
                    if (!g_zbuf || z < g_zbuf[idx]) {
                        if (g_zbuf) g_zbuf[idx] = z;
                        color_buf[idx] = lerp_color(vc[0], vc[1], vc[2], w0, w1, w2);
                    }
                }
            }
        }
    }
    return true;
}
