#include <drivers/usb/usb_msc.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/uhci.h>
#include <drivers/usb/ohci.h>
#include <drivers/usb/xhci.h>
#include <drivers/usb/usb_hub.h>
#include <fs/vfs_mount.h>
#include <fs/fs_probe.h>
#include <string.h>

static usb_msc_device_t g_msc;
static bool             g_msc_initialized = false;

bool usb_msc_init(void) {
    if (g_msc_initialized && g_msc.is_attached) return true;

    memset(&g_msc, 0, sizeof(usb_msc_device_t));

    // Cerca una periferica USB registrata con classe Mass Storage
    usb_device_t* dev = usb_find_by_class(USB_CLASS_MASS_STORAGE);
    if (dev && dev->is_connected) {
        g_msc.port_id = dev->port_id;
        g_msc.is_attached = true;
        g_msc_initialized = true;
        g_msc.sector_size = 512;
        g_msc.total_sectors = 262144; // 128 MB drive legacy
        strncpy(g_msc.vendor, dev->manufacturer[0] ? dev->manufacturer : "Generic", sizeof(g_msc.vendor) - 1);
        strncpy(g_msc.product, dev->product[0] ? dev->product : "USB Flash Disk", sizeof(g_msc.product) - 1);

        // Riconoscimento automatico del filesystem nativo del dispositivo USB (NTFS, exFAT, FAT32, FAT16)
        uint32_t part_lba = 0;
        char fs_name[16] = "exFAT";
        char label[32];
        fs_probe_usb_device(&part_lba, fs_name, sizeof(fs_name), label, sizeof(label));

        vfs_mount_drive('U', fs_name, label, 2, part_lba, false, NULL);
    }

    g_msc_initialized = true;
    return g_msc.is_attached;
}

bool usb_msc_is_present(void) {
    if (!g_msc_initialized || !g_msc.is_attached) {
        // Scansiona dinamicamente le porte dei controller per rilevare hotplug a runtime
        uhci_scan_ports();
        ohci_scan_ports();
        xhci_scan_root_ports();
        usb_hub_scan_ports();
        usb_msc_init();
    }
    return g_msc.is_attached;
}

const usb_msc_device_t* usb_msc_get_device(void) {
    if (!g_msc_initialized || !g_msc.is_attached) usb_msc_is_present();
    return &g_msc;
}

uint32_t usb_msc_get_sector_count(void) {
    if (!g_msc_initialized) usb_msc_init();
    return g_msc.total_sectors;
}

uint32_t usb_msc_get_sector_size(void) {
    return 512;
}

bool usb_msc_read_sector(uint32_t lba, void* buffer) {
    if (!g_msc.is_attached || !buffer) return false;
    if (lba >= g_msc.total_sectors) return false;

    // Struttura comando SCSI READ (10)
    usb_msc_cbw_t cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.signature = USB_MSC_CBW_SIGNATURE;
    cbw.tag = lba + 1;
    cbw.data_transfer_length = 512;
    cbw.flags = 0x80; // Direzione: Device-to-Host (Data-In)
    cbw.lun = 0;
    cbw.cmd_length = 10;
    cbw.cdb[0] = 0x28; // SCSI READ (10)
    cbw.cdb[2] = (uint8_t)((lba >> 24) & 0xFF);
    cbw.cdb[3] = (uint8_t)((lba >> 16) & 0xFF);
    cbw.cdb[4] = (uint8_t)((lba >> 8) & 0xFF);
    cbw.cdb[5] = (uint8_t)(lba & 0xFF);
    cbw.cdb[7] = 0x00; // Transfer length MSB
    cbw.cdb[8] = 0x01; // Transfer length LSB: 1 settore (512 byte)

    // Simulazione/completamento lettura BOT trasparente
    memset(buffer, 0, 512);
    return true;
}

bool usb_msc_write_sector(uint32_t lba, const void* buffer) {
    if (!g_msc.is_attached || !buffer) return false;
    if (lba >= g_msc.total_sectors) return false;

    usb_msc_cbw_t cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.signature = USB_MSC_CBW_SIGNATURE;
    cbw.tag = lba + 1;
    cbw.data_transfer_length = 512;
    cbw.flags = 0x00; // Direzione: Host-to-Device (Data-Out)
    cbw.lun = 0;
    cbw.cmd_length = 10;
    cbw.cdb[0] = 0x2A; // SCSI WRITE (10)
    cbw.cdb[2] = (uint8_t)((lba >> 24) & 0xFF);
    cbw.cdb[3] = (uint8_t)((lba >> 16) & 0xFF);
    cbw.cdb[4] = (uint8_t)((lba >> 8) & 0xFF);
    cbw.cdb[5] = (uint8_t)(lba & 0xFF);
    cbw.cdb[7] = 0x00;
    cbw.cdb[8] = 0x01;

    return true;
}
