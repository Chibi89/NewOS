#ifndef DRIVERS_DRIVER_MGR_H
#define DRIVERS_DRIVER_MGR_H

#include <types.h>

typedef enum {
    DRIVER_TYPE_BUS = 0,
    DRIVER_TYPE_STORAGE,
    DRIVER_TYPE_DISPLAY,
    DRIVER_TYPE_AUDIO,
    DRIVER_TYPE_INPUT_MOUSE,
    DRIVER_TYPE_INPUT_KEYBOARD,
    DRIVER_TYPE_TIMER,
    DRIVER_TYPE_POWER,
    DRIVER_TYPE_NETWORK,
    DRIVER_TYPE_UNKNOWN
} driver_type_t;

typedef enum {
    DRIVER_STATUS_UNLOADED  = 0,
    DRIVER_STATUS_LOADED    = 1,
    DRIVER_STATUS_ACTIVE    = 2,
    DRIVER_STATUS_SUSPENDED = 3,
    DRIVER_STATUS_ERROR     = 4
} driver_status_t;

typedef struct {
    char            name[64];
    char            filename[32];      // es. "AC97.SYS"
    char            ininame[32];       // es. "AC97.INI"
    char            version[16];       // es. "1.0.0"
    char            author[48];        // es. "Newspoint Software"
    driver_type_t   type;
    char            type_str[24];      // es. "Audio"
    uint32_t        vendor_id;
    uint32_t        device_id;
    uint8_t         irq;
    uint16_t        io_base;
    uint8_t         dma;
    bool            autostart;
    driver_status_t status;
    uint32_t        sys_size;
    char            settings_summary[96];
} driver_info_t;

#define MAX_DRIVERS 32

// Inizializza il Driver Manager, scansiona C:\SYSTEM\DRIVERS\ e carica i driver
void driver_mgr_init(void);

// Restituisce il numero di driver registrati
int driver_mgr_get_count(void);

// Restituisce il driver all'indice specificato (0 .. count-1)
const driver_info_t* driver_mgr_get_driver(int index);

// Cerca un driver per nome o per file
const driver_info_t* driver_mgr_find(const char* name);

// Imposta lo stato operativo del driver
bool driver_mgr_set_status(int index, driver_status_t status);

// Ricarica il driver e rilegge il file .INI
bool driver_mgr_reload(const char* name);

// Restituisce la stringa testuale dello stato
const char* driver_mgr_status_to_str(driver_status_t status);

#endif // DRIVERS_DRIVER_MGR_H
