# Regole Comportamentali per l'Agente AI — NewOS V1 (64-bit)

Le regole di comportamento, vincoli tecnici e direttive operative per lo sviluppo su questo progetto sono definite in:
- [`.agents/rules/comportamenti.md`](file:///c:/Users/morga/Desktop/MyOS/.agents/rules/comportamenti.md)
- [`rules/comportamenti.md`](file:///c:/Users/morga/Desktop/MyOS/rules/comportamenti.md)

### Riepilogo Regole Primarie:
1. **Budget Kernel**: Dimensione di `kernel.bin` rigorosamente inferiore a **491.520 byte (480 KB)**.
2. **Bootloader Stage 2**: `chs_count` sempre impostato a 960 settori (`boot/stage2.asm`).
3. **Qualità del Codice**: Zero warning, zero errori del compilatore, preservazione dei commenti.
4. **Validazione**: Compilazione con `node tools/build.js` e collaudo visivo in QEMU con screendump per ogni componente GUI.
5. **Comunicazione**: Risposte sempre in lingua italiana, sintetiche e con link cliccabili (`file:///`).
6. **Gerarchia File System**: File di sistema in `C:\NewOS\`, software utente/terze parti solo in `C:\ProgramFile`.
7. **Registro**: Sola lettura pre-desktop, scrittura consentita solo a sessione autenticata.
8. **Hardware Reale Bare-Metal**: Componenti vecchi e nuovi devono essere sviluppati e collaudati per l'uso su silicio fisico x86_64 e non solo su VM/emulatori (handshake BIOS/OS, temporizzazioni PHY, allineamenti DMA).
