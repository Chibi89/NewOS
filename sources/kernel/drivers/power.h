#ifndef DRIVERS_POWER_H
#define DRIVERS_POWER_H

#include <types.h>

typedef enum {
    POWER_PROFILE_BALANCED = 0,    // Bilanciato (consigliato: 60 FPS, throttle dopo 5s inattività, standby 3 min)
    POWER_PROFILE_SAVER,           // Risparmio energia (30 FPS, throttle aggressivo dopo 2s, standby 1 min)
    POWER_PROFILE_PERFORMANCE      // Prestazioni elevate (100 FPS costanti, nessun throttle, standby disattivato)
} power_profile_t;

typedef struct {
    bool ac_online;              // Rete elettrica CA collegata
    bool battery_present;        // Batteria rilevata
    bool is_charging;            // Batteria in stato di carica
    uint8_t battery_percent;     // Livello di carica residuo (0 - 100%)
    power_profile_t profile;     // Profilo di alimentazione corrente
    uint32_t screen_timeout_sec; // Timeout spegnimento monitor in secondi (0 = disattivato)
    uint64_t last_activity_ticks;// Ultimo tick di attività registrato
    bool is_screen_standby;      // Vero se il monitor è attualmente in sospensione DPMS
    uint32_t idle_seconds;       // Secondi trascorsi dall'ultima attività utente
} power_status_t;

// Inizializza il sottosistema di Power Management
void power_init(void);

// Notifica attività dell'utente (movimento mouse, clic, tasti premuti)
void power_notify_user_activity(void);

// Aggiorna lo stato di inattività e calcola l'intervallo di sleep ottimale (ms)
uint32_t power_update(void);

// Controlla e gestisce la modalità standby dello schermo
bool power_is_screen_standby(void);
void power_enter_screen_standby(void);
void power_exit_screen_standby(void);

// Controllo hardware di alimentazione
void power_shutdown(void);
void power_reboot(void);

// Configurazione e telemetria
power_status_t power_get_status(void);
void power_set_profile(power_profile_t profile);
void power_set_screen_timeout(uint32_t seconds);
uint32_t power_get_idle_seconds(void);

#endif // DRIVERS_POWER_H
