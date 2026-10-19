#include <drivers/speaker.h>
#include <drivers/timer.h>
#include <arch/x86_64/io.h>

void speaker_play_freq(uint32_t freq) {
    if (freq == 0) {
        speaker_stop();
        return;
    }
    uint32_t div = 1193180 / freq;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(div & 0xFF));
    outb(0x42, (uint8_t)((div >> 8) & 0xFF));

    uint8_t tmp = inb(0x61);
    if ((tmp & 0x03) != 0x03) {
        outb(0x61, tmp | 0x03);
    }
}

void speaker_stop(void) {
    uint8_t tmp = inb(0x61) & 0xFC;
    outb(0x61, tmp);
}

void speaker_tone(uint32_t freq, uint32_t duration_ms) {
    speaker_play_freq(freq);
    timer_sleep(duration_ms);
    speaker_stop();
}

void speaker_play_chime(void) {
    // Chime melodico stile Windows (Sol - Do - Re - Sol)
    speaker_tone(784, 80);   // G5
    timer_sleep(20);
    speaker_tone(1046, 100); // C6
    timer_sleep(20);
    speaker_tone(1175, 120); // D6
    timer_sleep(20);
    speaker_tone(1568, 220); // G6
}

void speaker_play_startup(void) {
    // Jingle di benvenuto stile Windows 2000 ricreato in frequenze sonore
    speaker_tone(523, 120); // C5
    speaker_tone(659, 120); // E5
    speaker_tone(784, 150); // G5
    speaker_tone(1046, 250); // C6
}
