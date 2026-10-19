#include <drivers/net/net_nic.h>
#include <drivers/net/e1000.h>
#include <drivers/net/rtl8139.h>
#include <drivers/net/wifi.h>
#include <drivers/pci.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/xhci.h>
#include <drivers/usb/usb_net.h>
#include <string.h>
#include <arch/x86_64/io.h>

static net_nic_t         g_active_nic;
static bool              g_nic_initialized = false;
static net_rx_handler_t  g_rx_handler = NULL;

static void serial_log(const char* s) {
    if (!s) return;
    while (*s) outb(0x3F8, *s++);
}

uint16_t net_htons(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}

uint16_t net_ntohs(uint16_t v) {
    return net_htons(v);
}

uint32_t net_htonl(uint32_t v) {
    return ((v & 0x000000FF) << 24) |
           ((v & 0x0000FF00) << 8)  |
           ((v & 0x00FF0000) >> 8)  |
           ((v & 0xFF000000) >> 24);
}

uint32_t net_ntohl(uint32_t v) {
    return net_htonl(v);
}

void net_format_ip(uint32_t ip, char* buf, size_t buf_size) {
    if (!buf || buf_size < 16) return;
    uint8_t* b = (uint8_t*)&ip;
    mini_snprintf(buf, buf_size, "%u.%u.%u.%u", (unsigned int)b[0], (unsigned int)b[1], (unsigned int)b[2], (unsigned int)b[3]);
}

void net_format_mac(const uint8_t mac[6], char* buf, size_t buf_size) {
    if (!buf || buf_size < 18) return;
    mini_snprintf(buf, buf_size, "%02X:%02X:%02X:%02X:%02X:%02X",
                  (unsigned int)mac[0], (unsigned int)mac[1], (unsigned int)mac[2],
                  (unsigned int)mac[3], (unsigned int)mac[4], (unsigned int)mac[5]);
}

uint32_t net_parse_ip(const char* ip_str) {
    if (!ip_str) return 0;
    uint32_t b[4] = {0, 0, 0, 0};
    int idx = 0;
    const char* p = ip_str;
    while (*p && idx < 4) {
        if (*p >= '0' && *p <= '9') {
            b[idx] = b[idx] * 10 + (*p - '0');
        } else if (*p == '.') {
            idx++;
        } else {
            break;
        }
        p++;
    }
    return (b[0]) | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

bool net_nic_init(void) {
    if (g_nic_initialized) return true;

    memset(&g_active_nic, 0, sizeof(net_nic_t));

    // Inizializza il Sottosistema USB (Legacy UHCI/OHCI 1.1 + xHCI + Foundation Classes)
    usb_subsystem_init();

    serial_log("[NET_NIC] Ricerca hardware Ethernet PCI...\n");

    // 1. Prova schede Intel E1000 (82540EM e varianti)
    pci_device_t pci_dev;
    uint16_t intel_dev_ids[] = { 0x100E, 0x1004, 0x100F, 0x1010, 0x1011, 0x1012, 0x101D, 0x1079, 0x107C };
    bool found_e1000 = false;

    for (size_t i = 0; i < sizeof(intel_dev_ids) / sizeof(intel_dev_ids[0]); i++) {
        if (pci_find_device(0x8086, intel_dev_ids[i], &pci_dev)) {
            serial_log("[NET_NIC] Trovato controller Intel E1000 (DeviceID=0x");
            char hex[8];
            mini_snprintf(hex, sizeof(hex), "%04X)\n", intel_dev_ids[i]);
            serial_log(hex);
            if (e1000_init(&pci_dev, &g_active_nic)) {
                found_e1000 = true;
                break;
            }
        }
    }

    // Inizializza il sottosistema Wi-Fi (cerca controller PCI Wi-Fi 7 / 6E / AC)
    pci_device_t wifi_pci;
    bool found_wifi_pci = false;
    uint16_t wifi_ids[] = { 0x272B, 0x272C, 0x2725, 0x2726, 0x2723, 0x2526, 0x8922, 0x8852 };
    for (size_t w = 0; w < sizeof(wifi_ids) / sizeof(wifi_ids[0]); w++) {
        uint16_t vend = (wifi_ids[w] == 0x8922 || wifi_ids[w] == 0x8852) ? 0x10EC : 0x8086;
        if (pci_find_device(vend, wifi_ids[w], &wifi_pci)) {
            found_wifi_pci = true;
            serial_log("[NET_NIC] Trovato controller Wi-Fi PCI (Vendor=0x");
            char hbuf[16];
            mini_snprintf(hbuf, sizeof(hbuf), "%04X, Dev=0x%04X)\n", vend, wifi_ids[w]);
            serial_log(hbuf);
            wifi_init(&wifi_pci, NULL);
            break;
        }
    }
    if (!found_wifi_pci) {
        // Inizializza controller Wi-Fi con parametri predefiniti / emulati
        wifi_init(NULL, NULL);
    }

    if (!found_e1000) {
        // 2. Prova controller Realtek RTL8139
        if (pci_find_device(0x10EC, 0x8139, &pci_dev)) {
            serial_log("[NET_NIC] Trovato controller Realtek RTL8139 Fast Ethernet.\n");
            rtl8139_init(&pci_dev, &g_active_nic);
        } else {
            // 3. Se presente controller Wi-Fi, attiva Wi-Fi come scheda principale
            if (wifi_is_present()) {
                serial_log("[NET_NIC] Attivazione controller Wi-Fi come interfaccia di rete primaria.\n");
                wifi_init(found_wifi_pci ? &wifi_pci : NULL, &g_active_nic);
            } else {
                serial_log("[NET_NIC] Nessuna scheda PCI rilevata. Attivazione Loopback Virtuale.\n");
                g_active_nic.type = NIC_TYPE_NONE;
                strncpy(g_active_nic.name, "NewSoft Virtual Loopback Adapter", sizeof(g_active_nic.name) - 1);
                g_active_nic.mac[0] = 0x00; g_active_nic.mac[1] = 0x15; g_active_nic.mac[2] = 0x5D;
                g_active_nic.mac[3] = 0x01; g_active_nic.mac[4] = 0x02; g_active_nic.mac[5] = 0x03;
                g_active_nic.link_up = true;
            }
        }
    }

    // Nessun indirizzo IP statico predefinito: gli indirizzi di rete
    // devono essere negoziati e ottenuti dinamicamente tramite DHCP dalla scheda di rete.
    g_active_nic.ip      = 0;
    g_active_nic.netmask = 0;
    g_active_nic.gateway = 0;
    g_active_nic.dns     = 0;

    g_nic_initialized = true;

    char mac_str[24];
    net_format_mac(g_active_nic.mac, mac_str, sizeof(mac_str));
    serial_log("[NET_NIC] Scheda attiva: ");
    serial_log(g_active_nic.name);
    serial_log(" - MAC: ");
    serial_log(mac_str);
    serial_log("\n");

    return true;
}

net_nic_t* net_nic_get_active(void) {
    if (!g_nic_initialized) net_nic_init();
    return &g_active_nic;
}

bool net_nic_send_packet(const void* data, uint16_t len) {
    if (!g_nic_initialized) net_nic_init();
    if (len == 0 || !data) return false;

    if (g_active_nic.type == NIC_TYPE_WIFI) {
        return wifi_send(data, len);
    } else if (g_active_nic.type == NIC_TYPE_E1000) {
        return e1000_send(data, len);
    } else if (g_active_nic.type == NIC_TYPE_RTL8139) {
        return rtl8139_send(data, len);
    } else {
        // Loopback virtuale: loop-back del pacchetto se c'e' un handler
        if (g_rx_handler) {
            g_rx_handler((const uint8_t*)data, len);
        }
        g_active_nic.tx_packets++;
        g_active_nic.tx_bytes += len;
        g_active_nic.rx_packets++;
        g_active_nic.rx_bytes += len;
        return true;
    }
}

void net_nic_set_rx_handler(net_rx_handler_t handler) {
    g_rx_handler = handler;
}

void net_nic_poll(void) {
    if (!g_nic_initialized || !g_rx_handler) return;

    if (g_active_nic.type == NIC_TYPE_WIFI) {
        wifi_poll(g_rx_handler);
    } else if (g_active_nic.type == NIC_TYPE_E1000) {
        e1000_poll(g_rx_handler);
    } else if (g_active_nic.type == NIC_TYPE_RTL8139) {
        rtl8139_poll(g_rx_handler);
    }
}
