/**
 * kernel/net/dns.h
 * Client e Servizio di Risoluzione DNS (Domain Name System) per NewOS V1 (64-bit)
 * Conforme a RFC 1035 (UDP porta 53)
 * (C) Copyright Newspoint Software - NewSoft - NewOS V1
 */

#ifndef NET_DNS_H
#define NET_DNS_H

#include <types.h>

// Inizializza il servizio DNS interno e la cache
void dns_init(void);

// Risolve un hostname (es. "google.it", "wikipedia.org") in un indirizzo IPv4 (network byte order).
// Ritorna l'IP valido se risolto, oppure 0 se fallito o non raggiungibile.
uint32_t dns_resolve(const char* hostname);

// Imposta manualmente il server DNS primario
void dns_set_server(uint32_t dns_ip);

// Restituisce il server DNS primario attivo
uint32_t dns_get_server(void);

// Svuota la cache DNS locale
void dns_flush_cache(void);

#endif // NET_DNS_H
