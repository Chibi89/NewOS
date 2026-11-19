#ifndef DRIVERS_NVME_H
#define DRIVERS_NVME_H

#include <types.h>

bool        nvme_init(void);
bool        nvme_is_present(void);
uint64_t    nvme_get_capacity_sectors(void);
uint32_t    nvme_get_sector_size(void);
const char* nvme_get_model(void);
bool        nvme_read_sectors(uint64_t lba, uint32_t count, void* buffer);
bool        nvme_write_sectors(uint64_t lba, uint32_t count, const void* buffer);

#endif // DRIVERS_NVME_H
