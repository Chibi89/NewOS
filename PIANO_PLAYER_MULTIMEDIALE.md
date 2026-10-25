# Piano di Implementazione: Riproduttore Multimediale Audio/Video — NewOS Player (`PLAYER.EXE`)

Il presente documento definisce la progettazione tecnica, l'architettura dei codec, la struttura dei file di catalogo, l'interfaccia grafica e la roadmap di implementazione per il riproduttore multimediale ufficiale di **NewOS V1 (64-bit)**.

---

## 1. Struttura delle Directory e Organizzazione File System

In conformità con le direttive ufficiali NewOS V1 (**Regola 6**: file di sistema in `C:\NewOS\`; **Regola Primaria 1**: inviolabilità di `C:\User\`):

```text
C:\
├── NewOS\
│   ├── MultimediaCodec\              [Librerie e moduli di decodifica audio/video .CDC]
│   │   ├── WAVDEC.CDC / .INI          [Audio: PCM RIFF Wave nativo 16/24-bit]
│   │   ├── MP3DEC.CDC / .INI          [Audio: MPEG-1/2 Audio Layer III universale]
│   │   ├── AACDEC.CDC / .INI          [Audio: Advanced Audio Coding per streaming e MP4]
│   │   ├── OPUSDEC.CDC / .INI         [Audio: Opus a bassa latenza e streaming real-time]
│   │   ├── FLACDEC.CDC / .INI         [Audio: FLAC / ALAC Lossless Studio Hi-Fi bit-perfect]
│   │   ├── AV1DEC.CDC / .INI          [Video: AV1 AOMedia — CODEC VIDEO PRINCIPALE PREDEFINITO (Open-Source, Royalty-Free)]
│   │   ├── H264DEC.CDC / .INI         [Video: H.264 / AVC - Fallback universale a massima compatibilità]
│   │   ├── H265DEC.CDC / .INI         [Video: H.265 / HEVC - Compressione raddoppiata per 4K/HDR (Legacy/Brevettato)]
│   │   ├── VP9DEC.CDC / .INI          [Video: VP9 Google open format per WebM/YouTube]
│   │   ├── AVIDEC.CDC / .INI          [Demuxer/Decoder: Contenitore AVI & Motion Video]
│   │   └── MP4DEC.CDC / .INI          [Demuxer/Decoder: Contenitore MP4 / ISOBMFF]
│   ├── PlayerCatalogy\               [Directory cataloghi, preferenze e playlist]
│   │   ├── PLAYER.INI                 [Configurazione globale del player]
│   │   ├── DEFAULT.PLAYLIST           [Elenco sequenziale dei percorsi dei file multimediali]
│   │   └── DEFAULT.PLAYLIST.INI       [Metadati avanzati della playlist: titoli, artisti, durate]
│   ├── Resources\
│   │   ├── Miusic\                    [Campioni audio di fabbrica precaricati all'avvio]
│   │   │   ├── WELCOME.WAV            [Brano sinfonico di benvenuto in formato PCM HQ]
│   │   │   └── SAMPLE.MP3             [Traccia audio dimostrativa MP3]
│   │   └── Videos\                    [Filmati dimostrativi di fabbrica precaricati]
│   │       ├── NEWOS_INTRO.AVI        [Video di presentazione NewOS V1 (Motion Video)]
│   │       └── SAMPLE.MP4             [Filmato dimostrativo ad alta risoluzione]
│   └── SystemProgram\
│       └── PLAYER.EXE                 [Eseguibile nativo del Riproduttore Multimediale]
└── User\
    └── [NomeUser]\ (es. Morgan\)
        ├── Miusic\                    [Cartella personale dell'utente per i propri file musicali]
        └── Video\                     [Cartella personale dell'utente per i propri filmati]
```

---

## 2. Architettura dell'Interfaccia Grafica Utente (GUI)

L'interfaccia utente di `PLAYER.EXE` adotta lo stile **NewOS Classic 32bpp** (ispirato a Windows Media Player 6.4 / 9 Series in tonalità grigio grafite e blu notte):

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│ [►] NewOS Media Player - WELCOME.WAV                           [—] [□] [✕] │
├─────────────────────────────────────────────────────────────────────────────┤
│ File   Riproduzione   Audio   Video   Playlist   Visualizzazione   Aiuto     │
├─────────────────────────────────────────────────────────────────────────────┤
│ ┌──────────────────────────────────────────────┐ ┌────────────────────────┐ │
│ │                                              │ │  Libreria & Catalogo   │ │
│ │                                              │ ├────────────────────────┤ │
│ │           SCHERMO VIDEO / DISPLAY            │ │ [♫] WELCOME.WAV  02:14 │ │
│ │                                              │ │ [♫] SAMPLE.MP3   03:45 │ │
│ │   [ Visualizzatore Spettro Audio 16-barre  ] │ │ [🎬] INTRO.AVI   00:48 │ │
│ │   [ oppure Frame Rendering Video 32bpp     ] │ │ [🎬] SAMPLE.MP4  01:20 │ │
│ │                                              │ │                        │ │
│ │                                              │ │ 📁 I miei brani        │ │
│ │                                              │ │ 📁 I miei video        │ │
│ └──────────────────────────────────────────────┘ └────────────────────────┘ │
│  01:15 ─────────────────●─────────────────────────── 02:14  [100% Vol] [🔈] │
├─────────────────────────────────────────────────────────────────────────────┤
│  [⏮]  [◀◀]  [ ▶ Play ]  [ ⏸ Pausa ]  [ ⏹ Stop ]  [▶▶]  [⏭]   [🔁]  [🔀]  [⛶] │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Componenti Principali della Finestra:
1. **Viewport Video / Visualizzatore Audio**:
   - **Modalità Video**: Rendering dei frame video a 32bpp con mantenimento dell'aspect ratio (16:9 o 4:3) e sfondo nero letterbox. Doppio clic o tasto `F11` attiva il **Full Screen Video**.
   - **Modalità Audio**: Quando è in riproduzione un file solo audio, lo schermo attiva automaticamente uno dei seguenti motori grafici selezionabili:
     - *Spettro di Frequenza (FFT Spectrum)*: 16 barre animate blu/verdi con picchi di caduta.
     - *Oscilloscopio Digitale*: Onda PCM in tempo reale sincronizzata sul buffer DMA audio.
     - *Cover Art / Logo*: Visualizzazione dell'icona del brano o del logo NewOS.
2. **Barra Temporale & Cursore (Seek Bar)**:
   - Indicatore del tempo trascorso e della durata totale (`01:15 / 02:14`).
   - Cursore trascinabile con il mouse per avanzare o riavvolgere la riproduzione in tempo reale.
3. **Controlli di Trasporto & Volume**:
   - Pulsanti standard 3D bevel: Play, Pausa, Stop, Traccia Precedente, Traccia Successiva.
   - Cursore Volume orizzontale graduato (0% - 100%) con pulsante Mute a 1 clic.
   - Tasti di modalità: Ripeti (Loop), Casuale (Shuffle), Schermo Intero (Fullscreen).
4. **Pannello Catalogo & Playlist Laterale (A scomparsa)**:
   - Visualizzazione ad albero o lista divisa per cartelle:
     - `♫ Musica di Sistema` (`C:\NewOS\Resources\Miusic\`)
     - `🎬 Video di Sistema` (`C:\NewOS\Resources\Videos\`)
     - `📁 Musica Utente` (`C:\User\[NomeUser]\Miusic\`)
     - `📁 Video Utente` (`C:\User\[NomeUser]\Video\`)
   - Pulsanti rapidi: `[+] Aggiungi File`, `[-] Rimuovi`, `[💾 Salva Playlist]`.

---

## 3. Specifiche dei File di Catalogo e Configurazione (`PlayerCatalogy\`)

### 3.1 File di Configurazione Globale: `C:\NewOS\PlayerCatalogy\PLAYER.INI`
```ini
[General]
Version=1.0.0
Volume=85
Mute=0
RepeatMode=0
Shuffle=0
VisualizerMode=1
ShowPlaylistPanel=1
LastPlayedFile=C:\NewOS\Resources\Miusic\WELCOME.WAV
LastPositionMs=0

[Video]
PrimaryCodec=AV1
FallbackCodec=H264
HardwareAcceleration=AutoDetect
AspectRatio=Auto
HardwareScaling=1
LetterboxColor=0x000000
SubtitleEnabled=1

[LibraryPaths]
SystemMusic=C:\NewOS\Resources\Miusic\
SystemVideo=C:\NewOS\Resources\Videos\
UserMusic=C:\User\Morgan\Miusic\
UserVideo=C:\User\Morgan\Video\
```

### 3.2 File Elenco Playlist: `C:\NewOS\PlayerCatalogy\DEFAULT.PLAYLIST`
File di testo semplice formattato riga per riga per caricamento ultra-veloce:
```text
C:\NewOS\Resources\Miusic\WELCOME.WAV
C:\NewOS\Resources\Miusic\SAMPLE.MP3
C:\NewOS\Resources\Videos\NEWOS_INTRO.AVI
C:\NewOS\Resources\Videos\SAMPLE.MP4
```

### 3.3 File Metadati Playlist: `C:\NewOS\PlayerCatalogy\DEFAULT.PLAYLIST.INI`
```ini
[PlaylistInfo]
Name=Catalogo Multimediale Predefinito
TotalItems=4
CreatedDate=2026-10-23
Author=NewSoft Media Subsystem

[Item_0]
Path=C:\NewOS\Resources\Miusic\WELCOME.WAV
Title=NewOS Welcome Symphony
Artist=Newspoint Audio Lab
Type=Audio
DurationSeconds=134
SampleRate=44100
Channels=2

[Item_1]
Path=C:\NewOS\Resources\Miusic\SAMPLE.MP3
Title=Digital Groove Sample
Artist=NewSoft Synthesizer
Type=Audio
DurationSeconds=225
Bitrate=192

[Item_2]
Path=C:\NewOS\Resources\Videos\NEWOS_INTRO.AVI
Title=Presentazione NewOS V1 64-bit
Type=Video
DurationSeconds=48
Resolution=640x360
FPS=30

[Item_3]
Path=C:\NewOS\Resources\Videos\SAMPLE.MP4
Title=Dimostrazione Video HD
Type=Video
DurationSeconds=80
Resolution=640x360
FPS=30
```

---

## 4. Architettura Tecnica dei Motori Audio, Video e Codec Modulari

### 4.0 Architettura Modulare dei Codec (`C:\NewOS\MultimediaCodec\`)
Per garantire massima espandibilità, stabilità ed isolamento della memoria, i componenti di decodifica audio e video risiedono come moduli indipendenti in **`C:\NewOS\MultimediaCodec\`** con estensione **`.CDC`** (Codec Driver Component):
- **Tabella Entrypoint dei Codec (`media_codec_interface_t`)**:
  Ogni modulo `.CDC` espone una struttura di interfaccia standard a 64-bit:
  ```c
  typedef struct {
      uint32_t magic;              // Magic Signature 'NCDC' (0x4344434E)
      char name[32];               // Es: "NewSoft MPEG Layer-3 Audio Decoder"
      char extensions[48];         // Estensioni gestite: ".MP3" o ".WAV" o ".AVI" o ".MP4"
      uint32_t version;            // Versione codec (es: 0x00010000)
      bool (*probe)(const uint8_t* header, uint32_t size);
      void* (*open_stream)(const char* path, media_info_t* out_info);
      bool (*read_audio_pcm)(void* handle, uint8_t* out_buf, uint32_t bytes, uint32_t* out_bytes);
      bool (*read_video_frame)(void* handle, uint32_t* out_frame_32bpp, uint32_t* out_timestamp_ms);
      bool (*seek_ms)(void* handle, uint32_t timestamp_ms);
      void (*close_stream)(void* handle);
  } media_codec_interface_t;
  ```
- **File Metadati Associati (`.INI`)**:
  Ogni file binario `.CDC` è accompagnato da un file `.INI` (es. `MP3DEC.INI`, `AVIDEC.INI`) che specifica nome del fornitore, bit-depth supportato, frequenze di campionamento e opzioni di buffer.
- **Rilevamento Dinamico all'Avvio**:
  `PLAYER.EXE` all'avvio esegue la scansione di `C:\NewOS\MultimediaCodec\`: carica la lista dei codec disponibili e registra automaticamente i demuxer e i decoder. Per aggiungere il supporto a un nuovo formato audio/video in futuro, sarà sufficiente inserire il nuovo file `.CDC` in questa cartella senza dover ricompilare il player o il kernel.

### 4.1 Suite Codec Audio (`Lossy` vs `Lossless`)
Nel campo audio la distinzione architetturale fondamentale all'interno di NewOS è tra codec **Lossy** (compressione con perdita di dettaglio percettiva per minimizzare drasticamente l'occupazione di memoria e banda) e codec **Lossless** (qualità da studio bit-perfect senza alcuna perdita):

1. **AAC (`AACDEC.CDC`) — Advanced Audio Coding (Lossy High-Efficiency)**:
   - **Inquadramento**: Il successore spirituale dell'MP3. È l'attuale standard de facto per lo streaming su scala globale (YouTube, Twitch, Apple Music), per l'intero ecosistema Apple e per le tracce audio nei container video MP4/M4A.
   - **Prestazioni & Bitrate**: Offre una qualità sonora eccellente a bitrate medio-bassi (96 - 256 kbps), supportando profili LC (Low Complexity) e HE-AAC (High Efficiency).
   - **Ruolo in NewOS**: Decodifica ad alta efficienza per stream MP4 ed estrazioni audio da filmati web.

2. **MP3 (`MP3DEC.CDC`) — MPEG-1/2 Audio Layer III (Lossy Universal Legacy)**:
   - **Inquadramento**: Nonostante sia tecnologicamente superato da decenni, resiste come formato universale per la musica "legacy" grazie alla compatibilità totale con qualsiasi dispositivo e piattaforma sul pianeta.
   - **Prestazioni**: Parsing snello con dequantizzazione a tabelle intere a 64-bit; bassissimo overhead computazionale sulla CPU in esecuzione Ring 0 / Ring 3.
   - **Ruolo in NewOS**: Riproduzione istantanea di librerie musicali preesistenti e retrocompatibilità totale.

3. **Opus (`OPUSDEC.CDC`) — Real-Time Low Latency (Lossy Dynamic)**:
   - **Inquadramento**: Il re indiscusso della bassissima latenza e dello streaming audio in tempo reale. Codec open-source moderno e royalty-free adottato da Discord, WhatsApp per le note vocali e WebRTC per le chiamate vocali interattive.
   - **Prestazioni**: Architettura dinamica SILK/CELT capace di adattarsi istantaneamente alla qualità della connessione o del clock audio, campionando da 8 kHz fino a 48 kHz con latenze inferiori a 20 ms.
   - **Ruolo in NewOS**: Streaming live, chiamate e riproduzione di file audio web di ultima generazione.

4. **FLAC & ALAC (`FLACDEC.CDC`) — Audio Lossless Studio Hi-Fi**:
   - **Inquadramento**: I codec lossless (senza alcuna perdita di dettaglio) per antonomasia. Il **FLAC** (standard libero e open-source) e l'**ALAC** (Apple Lossless Audio Codec) comprimono l'audio come un vero e proprio archivio ZIP: quando viene decompresso in memoria, l'audio PCM è matematicamente identico al master originale di studio.
   - **Prestazioni**: Supporto a profondità di campionamento a 16-bit e 24-bit con frequenze fino a 192 kHz; standard adottato dalle piattaforme di streaming Hi-Fi (Tidal, Apple Music Lossless).
   - **Ruolo in NewOS**: Riproduzione audio bit-perfect per audiofili e professionisti audio.

5. **WAV PCM (`WAVDEC.CDC`) — Audio di Sistema Non Compresso**:
   - **Inquadramento**: Formato nativo RIFF/WAVE a 8/16-bit PCM.
   - **Ruolo in NewOS**: Inviato direttamente al controller audio AC97 / HD Audio tramite DMA circolare senza de-compressione, ideale per suoni di sistema a latenza zero.

6. **Spectrum Analyzer & Oscilloscopio Real-Time**:
   - Campionamento di blocchi da 64/128 campioni PCM estratti direttamente dal buffer DMA per alimentare il visualizzatore a 16 barre spettrali o l'onda oscilloscopica a video.

---

### 4.2 Suite Codec Video & Demuxer
I moderni codec video si sfidano sul terreno dell'efficienza di compressione e sul supporto all'accelerazione hardware (fondamentale per preservare la batteria di laptop e PC e minimizzare i cicli CPU in x86_64 bare-metal).

In **NewOS V1**, la scelta architetturale primaria è di **adottare AV1 come CODEC VIDEO PRINCIPALE E PREDEFINITO**, poiché unisce la massima efficienza oggi esistente alla totale libertà da brevetti proprietari e royalties commerciali:

1. **AV1 (`AV1DEC.CDC`) — CODEC VIDEO PRINCIPALE E PREDEFINITO DI NEWOS V1 (AOMedia Open-Source & Royalty-Free)**:
   - **Scelta Architetturale Primaria**: **Codec video ufficiale di riferimento per NewOS V1**. Essendo completamente **gratuito, open-source e privo di brevetti con royalties**, garantisce la sovranità tecnologica del sistema operativo senza costi di licenza o vincoli proprietari.
   - **Inquadramento & Consorzio**: Sviluppato dall'Alliance for Open Media con il supporto congiunto dei maggiori leader mondiali del web e dell'hardware (Google, Netflix, Amazon, Apple, Meta, Intel, AMD, NVIDIA).
   - **Efficienza di Compressione Straordinaria**: Supera di circa il **30% l'efficienza di compressione di HEVC/H.265** e di oltre il **50% quella di H.264**, permettendo lo streaming e la riproduzione in Full HD, 4K e persino 8K con bitrate e occupazione disco minimi.
   - **Accelerazione Hardware & Pipeline Bare-Metal**: Integra l'interfacciamento con i decoder hardware presenti in tutte le moderne GPU (Intel Arc/Xe, AMD RDNA2/3, NVIDIA RTX 30/40), abbinato a un motore di decodifica software a 64-bit multi-thread SIMD (AVX2/SSE4) per macchine bare-metal e macchine virtuali.
   - **Ruolo in NewOS**: Motore di decodifica predefinito di `PLAYER.EXE` richiamato per default su flussi video web, file `.MP4`, `.MKV`, `.WEBM` e file nativi `.AV1`.

2. **H.264 / AVC (`H264DEC.CDC`) — Fallback Universale a Massima Compatibilità**:
   - **Inquadramento**: Ha più di vent'anni ma gira su qualsiasi dispositivo e GPU esistente al mondo.
   - **Utilizzo Oggi**: Impiegato in NewOS come **codec secondario di fallback** per assicurare la retrocompatibilità totale con vecchie registrazioni o macchine prive di supporto AV1, pur richiedendo molta più banda e spazio su disco rispetto ad AV1.
   - **Ruolo in NewOS**: Fallback trasparente per file video legacy `.MP4`, `.AVI` ed `.MKV`.

3. **H.265 / HEVC (`H265DEC.CDC`) — Codec Secondario Alta Efficienza (Legacy 4K & HDR)**:
   - **Inquadramento**: Standard per contenuti 4K Ultra HD e Blu-ray 4K commercializzati. Offre un'ottima compressione ma è vincolato da pesanti royalties e complesse licenze proprietarie.
   - **Ruolo in NewOS**: Modulo secondario opzionale per la riproduzione di flussi 4K commerciali già codificati in formato HEVC.

4. **VP9 (`VP9DEC.CDC`) — Google Open Video (Motore WebM Open)**:
   - **Inquadramento**: Sviluppato da Google come alternativa libera ad H.265 prima dell'avvento di AV1.
   - **Ruolo in NewOS**: Decodifica di video open WebM e container open a blocchi variabili 32x32.

5. **AVI & Motion Video (`AVIDEC.CDC`) — Contenitore Leggero**:
   - Demuxer RIFF AVI con supporto a Motion JPEG (MJPEG) e stream RGB raw per collaudi rapidi ed esecuzione senza lag anche in ambienti virtualizzati QEMU/Bochs.

6. **MP4 Demuxer (`MP4DEC.CDC`) — ISOBMFF Container Parser**:
   - Analisi degli atom ISOBMFF (`ftyp`, `moov`, `trak`, `mdia`, `minf`, `stbl`, `mdat`) con priorità di aggancio: estrae in via prioritaria lo stream **AV1** (o H.264/H.265 come fallback) e lo stream audio associato (**Opus** / AAC / MP3).

7. **Motore di Sincronizzazione A/V (Audio/Video Clock Sync)**:
   - Sistema di clock sincronizzato sul consumo effettivo dei campioni nel ring buffer DMA dell'AC97. Se il frame video decodificato si trova in anticipo viene sincronizzato con sleep di precisione; se si trova in ritardo, il motore adotta strategie di frame-skipping controllato per preservare l'integrità del flusso audio senza interruzioni o glitch.

---

### 4.3 Matrice Comparativa Generale dei Codec

| Codec | Tipo | Modulo `.CDC` | Ruolo & Priorità NewOS | Efficienza / Compressione | Modello Licenza | Risoluzione Target |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **AV1** | Video | `AV1DEC.CDC` | ⭐ **PRINCIPALE / PREDEFINITO** | **Massima** (~30% sup. a HEVC, ~50% a H.264) | **Open-Source / Gratuito / Royalty-Free** | 1080p, 4K UHD, 8K |
| **H.264 / AVC** | Video | `H264DEC.CDC` | Fallback Universale Legacy | Base (Compatibilità universale) | Brevettato (MPEG-LA) | SD, 720p, 1080p |
| **H.265 / HEVC** | Video | `H265DEC.CDC` | Secondario (Contenuti HDR/4K) | ~2x rispetto ad H.264 | Royalties proprietarie elevate | 4K Ultra HD, HDR |
| **VP9** | Video | `VP9DEC.CDC` | Open Legacy (WebM) | Simile a HEVC | **Open-Source / Royalty-Free** | 1080p, 4K |
| **Opus** | Audio | `OPUSDEC.CDC` | ⭐ **PRINCIPALE REAL-TIME & WEB** | **Altissima** a bassissima latenza | **Open-Source / Royalty-Free** | Voce & Musica (fino a 48 kHz) |
| **AAC** | Audio | `AACDEC.CDC` | Standard Streaming & MP4 | Ottima a 96-256 kbps | Brevettato (Via Licensing) | Stereo, 5.1, Multicanale |
| **MP3** | Audio | `MP3DEC.CDC` | Fallback Universale Legacy | Buona a 128-320 kbps | Brevetti scaduti (Public Domain) | Stereo 44.1/48 kHz |
| **FLAC / ALAC** | Audio | `FLACDEC.CDC` | ⭐ **PRINCIPALE LOSSLESS HI-FI** | Lossless bit-perfect (come uno ZIP) | **Open (FLAC) / Free (ALAC)** | 16/24-bit fino a 192 kHz |
| **WAV PCM** | Audio | `WAVDEC.CDC` | Suoni di Sistema Raw DMA | Non compresso (1:1 a latenza zero) | Libero | 8/16-bit 44.1 kHz |

---

## 5. Integrazione nel Sistema Operativo e Registro

### 5.1 Associazioni File nel Registro (`HKEY_CLASSES_ROOT`)
In `kernel/fs/registry.c`, registrazione delle estensioni con icona e comando di avvio automatico con doppio clic:
- `.AV1` -> `av1file` -> `C:\NewOS\SystemProgram\PLAYER.EXE "%1"` *(Principale Video)*
- `.WEBM` -> `webmfile` -> `C:\NewOS\SystemProgram\PLAYER.EXE "%1"`
- `.MP4` -> `mp4file` -> `C:\NewOS\SystemProgram\PLAYER.EXE "%1"` *(Priorità decodifica: AV1)*
- `.AVI` -> `avifile` -> `C:\NewOS\SystemProgram\PLAYER.EXE "%1"`
- `.WAV` -> `wavfile` -> `C:\NewOS\SystemProgram\PLAYER.EXE "%1"` *(Principale Audio Raw)*
- `.OPUS` -> `opusfile` -> `C:\NewOS\SystemProgram\PLAYER.EXE "%1"` *(Principale Audio Real-Time)*
- `.FLAC` -> `flacfile` -> `C:\NewOS\SystemProgram\PLAYER.EXE "%1"` *(Principale Audio Lossless)*
- `.MP3` -> `mp3file` -> `C:\NewOS\SystemProgram\PLAYER.EXE "%1"`
- `.PLAYLIST` -> `playlistfile` -> `C:\NewOS\SystemProgram\PLAYER.EXE "%1"`

### 5.2 Integrazione nell'Albero di Sistema (`tools/tree_manager.js`)
Aggiornamento di `dirsToCreate` per generare automaticamente all'atto del popolamento di `build/bin`:
- `NewOS/MultimediaCodec`
- `NewOS/PlayerCatalogy`
- `NewOS/Resources/Miusic`
- `NewOS/Resources/Videos`
- `User/Morgan/Miusic`
- `User/Morgan/Video`
- `User/Default/Miusic`
- `User/Default/Video`

---

## 6. Roadmap Operativa di Implementazione

| Fase | Modulo / Attività | File Principali | Obiettivo |
| :--- | :--- | :--- | :--- |
| **Fase 1** | Struttura Cartelle, Risorse e Playlist | `tools/tree_manager.js`, `resources/` | Creazione delle cartelle di sistema (inclusa `NewOS/MultimediaCodec`), generazione dei sample audio/video e file `PLAYER.INI`, `DEFAULT.PLAYLIST`, `DEFAULT.PLAYLIST.INI` |
| **Fase 2** | Sottosistema Codec Modulari (.CDC) | `MultimediaCodec/wavdec.c`, `MultimediaCodec/mp3dec.c` | Implementazione dell'interfaccia `media_codec_interface_t` per WAV e MP3 con streaming DMA AC97 |
| **Fase 3** | Codec Video & Demuxer Container | `MultimediaCodec/avidec.c`, `MultimediaCodec/mp4dec.c` | Demuxing AVI e MP4, estrazione frame video, timing sincronizzato A/V e frame scaler 32bpp |
| **Fase 4** | Interfaccia Grafica Utente `PLAYER.EXE` | `kernel/apps/player.c`, `kernel/apps/player.h` | Finestra GUI NewOS Classic, viewport video/spettro, seek bar interattiva, controlli di trasporto, volume e pannello playlist |
| **Fase 5** | Associazioni Registro & Explorer | `kernel/fs/registry.c`, `kernel/apps/explorer.c` | Apertura immediata a doppio clic da Explorer per file musicali e video |
| **Fase 6** | Compilazione, Budget Kernel & Test QEMU | `tools/build.js`, `test_player.js` | Validazione visiva e sonora con screendump e riproduzione fluida su AC97 |
