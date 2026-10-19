#ifndef DRIVERS_NET_WIFI_H
#define DRIVERS_NET_WIFI_H

#include <types.h>
#include <drivers/pci.h>
#include <drivers/net/net_nic.h>

#define WIFI_MAX_SCAN_ENTRIES 16
#define WIFI_SSID_MAX_LEN     34
#define WIFI_KEY_MAX_LEN      64

#define WIFI_BAND_MASK_2_4GHZ (1U << 0)
#define WIFI_BAND_MASK_5GHZ   (1U << 1)
#define WIFI_BAND_MASK_6GHZ   (1U << 2)

typedef enum {
    WIFI_SEC_OPEN = 0,
    WIFI_SEC_WEP,
    WIFI_SEC_WPA2_PSK,
    WIFI_SEC_WPA3_SAE
} wifi_security_t;

typedef enum {
    WIFI_BAND_2_4GHZ = 0,
    WIFI_BAND_5GHZ,
    WIFI_BAND_6GHZ // Wi-Fi 6E / Wi-Fi 7 (fino a 5.0 GB/s)
} wifi_band_t;

typedef enum {
    WIFI_BUS_PCIE = 0,
    WIFI_BUS_USB,
    WIFI_BUS_EMULATED
} wifi_bus_t;

typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_SCANNING,
    WIFI_STATE_AUTHENTICATING,
    WIFI_STATE_ASSOCIATING,
    WIFI_STATE_CONNECTED
} wifi_state_t;

typedef struct {
    char            ssid[WIFI_SSID_MAX_LEN];
    uint8_t         bssid[6];
    int8_t          rssi_dbm;        // es. -42 dBm
    uint8_t         channel;         // es. 1, 6, 36, 149, 37 (6GHz)
    wifi_band_t     band;
    uint32_t        frequency_mhz;   // es. 2437, 5180, 5975 MHz
    uint32_t        max_rate_mbps;   // Capacità massima dell'Access Point (es. 5000 = 5.0 GB/s)
    wifi_security_t security;
    uint8_t         signal_percent;  // 0..100%
} wifi_ap_t;

typedef struct {
    bool            present;
    wifi_bus_t      bus_type;
    wifi_state_t    state;
    char            adapter_name[64];
    char            hw_standard[32];     // es. "Wi-Fi 7 (802.11be)" o "Wi-Fi 5 (802.11ac)"
    uint32_t        hw_max_rate_mbps;    // Limite fisico hardware dell'adattatore (es. 1000 Mbps = 1 GB/s o 5000 Mbps = 5 GB/s)
    uint8_t         supported_bands_mask;// Maschera bande supportate fisicamente (2.4GHz, 5GHz, 6GHz)
    uint8_t         mac[6];
    char            connected_ssid[WIFI_SSID_MAX_LEN];
    uint8_t         connected_bssid[6];
    uint32_t        link_speed_mbps;     // Velocità effettivamente negoziata: min(hw_max, ap_max)
    uint8_t         channel;
    wifi_band_t     band;
    uint32_t        frequency_mhz;
    int8_t          rssi_dbm;
    uint8_t         signal_percent;
    wifi_security_t security;
    uint64_t        tx_bytes;
    uint64_t        rx_bytes;
    uint32_t        tx_packets;
    uint32_t        rx_packets;
    uint32_t        mmio_base;
    uint8_t         irq;
    uint16_t        pci_vendor_id;
    uint16_t        pci_device_id;
} wifi_info_t;

// Inizializza il controller Wi-Fi rilevando l'hardware reale da bus PCI
bool wifi_init(const pci_device_t* pci_dev, net_nic_t* nic);

// Verifica se la scheda Wi-Fi è rilevata e inizializzata
bool wifi_is_present(void);

// Restituisce informazioni e stato del controller Wi-Fi (compresi limiti fisici e velocità negoziata)
bool wifi_get_info(wifi_info_t* out_info);

// Imposta manualmente o per test il tetto massimo hardware (es. 1000 Mbps per scheda da 1 GB/s)
void wifi_set_hardware_cap(uint32_t max_speed_mbps, const char* standard_name);

// Scansiona le reti wireless disponibili filtrando o marcando la compatibilità radio
int  wifi_scan(wifi_ap_t* out_list, int max_entries);

// Connette a una rete wireless specificata rispettando rigorosamente il limite dell'adattatore
bool wifi_connect(const char* ssid, const char* passphrase);

// Disconnette dalla rete wireless attiva
bool wifi_disconnect(void);

// Invio pacchetto Ethernet tramite interfaccia Wi-Fi
bool wifi_send(const void* data, uint16_t len);

// Polling ricezione pacchetti Wi-Fi
void wifi_poll(net_rx_handler_t rx_handler);

// Restituisce il nome testuale della banda
const char* wifi_band_to_str(wifi_band_t band);

// Restituisce il nome testuale del tipo di sicurezza
const char* wifi_security_to_str(wifi_security_t sec);

// Restituisce il nome testuale del bus di collegamento
const char* wifi_bus_to_str(wifi_bus_t bus);

#endif // DRIVERS_NET_WIFI_H
