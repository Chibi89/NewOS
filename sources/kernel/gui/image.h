#ifndef GUI_IMAGE_H
#define GUI_IMAGE_H

#include <types.h>

#ifndef _IMAGE_T_DEFINED
#define _IMAGE_T_DEFINED
typedef struct image_s {
    int32_t   width;
    int32_t   height;
    uint32_t* pixels; // Array di pixel 0xAARRGGBB allocati nell'heap
} image_t;
#endif

// Decodifica un file immagine (.BMP o .JPG) dal percorso VFS specificato
image_t* image_load_from_file(const char* vfs_path);

// Decodificatori da buffer in memoria
image_t* image_decode_bmp(const uint8_t* data, uint32_t size);
image_t* image_decode_jpg(const uint8_t* data, uint32_t size);

// Ridimensiona un'immagine alla risoluzione desiderata (es. 1024x768 per lo sfondo del desktop)
image_t* image_resize(const image_t* src, int32_t new_w, int32_t new_h);

// Rilascia la memoria allocata per l'immagine
void image_free(image_t* img);

#endif // GUI_IMAGE_H
