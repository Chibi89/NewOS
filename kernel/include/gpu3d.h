#ifndef DRIVERS_GPU3D_H
#define DRIVERS_GPU3D_H

#include <types.h>

typedef enum {
    GPU_BACKEND_NONE = 0,
    GPU_BACKEND_VMSVGA,
    GPU_BACKEND_VIRTIO_GPU,
    GPU_BACKEND_INTEL_GEN,
    GPU_BACKEND_SOFT3D
} gpu_backend_type_t;

typedef struct {
    float x, y, z;
    uint32_t color;
} gpu3d_vertex_t;

typedef struct {
    bool               initialized;
    gpu_backend_type_t backend;
    bool               hw_2d_accel;
    bool               hw_3d_accel;
    uint32_t           width;
    uint32_t           height;
    char               adapter_name[64];
} gpu3d_info_t;

bool                gpu3d_init(uint32_t width, uint32_t height, uint64_t lfb_phys);
const gpu3d_info_t* gpu3d_get_info(void);
bool                gpu3d_rect_copy(int32_t sx, int32_t sy, int32_t dx, int32_t dy, int32_t w, int32_t h);
bool                gpu3d_update_rect(int32_t x, int32_t y, int32_t w, int32_t h);
bool                gpu3d_clear_3d(uint32_t* color_buf, uint32_t w, uint32_t h, uint32_t argb);
bool                gpu3d_draw_mesh(uint32_t* color_buf, uint32_t w, uint32_t h,
                                    const gpu3d_vertex_t* verts, uint32_t tri_count,
                                    float angle_x, float angle_y);

#endif // DRIVERS_GPU3D_H
