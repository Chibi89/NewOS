#ifndef DRIVERS_USB_MSC_H
#define DRIVERS_USB_MSC_H

#include <types.h>
#include <drivers/usb/usb.h>

#define USB_MSC_CBW_SIGNATURE 0x43425355 // "USBC"
#define USB_MSC_CSW_SIGNATURE 0x53425355 // "USBS"

// Command Block Wrapper (CBW) - 31 byte
typedef struct __attribute__((packed)) {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_transfer_length;
    uint8_t  flags;
    uint8_t  lun;
    uint8_t  cmd_length;
    uint8_t  cdb[16];
} usb_msc_cbw_t;

// Command Status Wrapper (CSW) - 13 byte
typedef struct __attribute__((packed)) {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_residue;
    uint8_t  status; // 0 = Success, 1 = Failed, 2 = Phase Error
} usb_msc_csw_t;

typedef struct {
    uint8_t  port_id;
    uint32_t total_sectors;
    uint32_t sector_size;
    bool     is_attached;
    char     vendor[16];
    char     product[24];
} usb_msc_device_t;

bool                     usb_msc_init(void);
bool                     usb_msc_is_present(void);
const usb_msc_device_t*  usb_msc_get_device(void);
uint32_t                 usb_msc_get_sector_count(void);
uint32_t                 usb_msc_get_sector_size(void);
bool                     usb_msc_read_sector(uint32_t lba, void* buffer);
bool                     usb_msc_write_sector(uint32_t lba, const void* buffer);

#endif // DRIVERS_USB_MSC_H
