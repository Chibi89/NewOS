# Specifica Tecnica Completa dei Temi Visivi — NewOS V1 (64-bit)

Questo documento costituisce la specifica tecnica esaustiva e ufficiale per la creazione, personalizzazione ed estensione dei temi visivi del sistema operativo **NewOS V1**.

Tutti i parametri descritti sono gestiti in modo dinamico dalla libreria out-of-kernel [`THEMEMGR.DLL`](file:///c:/Users/morga/Desktop/MyOS/SystemLibrary/theme_mgr.c) (allocata in `C:\NewOS\SystemLibrary\`) e applicati in tempo reale dal Window Manager del Kernel ([`kernel/gui/wm.c`](file:///c:/Users/morga/Desktop/MyOS/kernel/gui/wm.c)).

---

## 1. Architettura del Sistema Temi

Il sottosistema temi di NewOS V1 adotta un'architettura rigorosamente disaccoppiata e data-driven:

```
               [ File .THEMES in C:\NewOS\Resource\Themes\ ]
                                    │
                                    ▼
       [ Libreria Dinamica Out-of-Kernel: THEMEMGR.DLL (SystemLibrary) ]
                                    │  (Parsing INI, Calcolo Colori, Sorting)
                                    ▼
       [ Interfaccia Kernel: kernel/gui/theme_loader.c ]
                                    │  (theme_info_t struct)
                                    ▼
       [ Window Manager: kernel/gui/wm.c & wm_draw_themed_* ]
                                    │
              ┌─────────────────────┼─────────────────────┐
              ▼                     ▼                     ▼
     [ Finestre & Cornici ]   [ Taskbar & Start ]   [ Controlli & Tab ]
```

### Vantaggi Architetturali:
1. **Preservazione del Budget Kernel**: La logica di decodifica testuale INI e scansione directory risiede interamente in `THEMEMGR.DLL`, consentendo a `kernel.bin` di mantenersi ampiamente al di sotto del limite di 480 KB (334 KB attuali).
2. **Caricamento a Caldo (Hot-Reload)**: L'aggiunta di un nuovo file `.THEMES` nella cartella `C:\NewOS\Resource\Themes\` rende immediatamente disponibile il tema nel **Pannello di Controllo -> Proprietà Schermo -> Tema** senza ricompilare il kernel.
3. **Controllo a Grana Fine**: Ogni singolo elemento grafico (trasparenza, raggio di curvatura, ombre, bottoni, bordi, tray e taskbar) è configurabile tramite variabili dichiarative.

---

## 2. Sintassi e Tipi di Dato dei File `.THEMES`

I file tema hanno estensione `.THEMES` (o `.THM`) e seguono il formato standard **INI**. 
I commenti iniziano con punto e virgola (`;`).

| Tipo di Dato | Formato | Esempio | Descrizione |
|---|---|---|---|
| **Stringa** | Testo alfanumerico | `Name=Mica Light` | Nome, percorso file o etichetta. |
| **Booleano** | `1` o `0` | `DropShadow=1` | `1` per abilitato, `0` per disabilitato. |
| **Intero (Pixel)** | Decimale | `CornerRadius=8` | Misure geometriche (pixel). |
| **Colore 24-bit** | Esadecimale `0xRRGGBB` | `ActiveTitle=0x0078D4` | Colore RGB senza canale alpha. |
| **Opacità (Alpha)** | Decimale `0` - `255` | `SurfaceAlpha=235` | `0` = trasparente, `255` = opaco al 100%. |

---

## 3. Specifica Completa delle Sezioni e delle Variabili

### 3.1. Sezione `[Theme]` — Metadati e Motore Grafico

| Chiave | Tipo | Valori Tipici | Descrizione |
|---|---|---|---|
| `Name` | Stringa | `Windows 11 Mica Light` | Nome visualizzato nella lista dei temi del pannello di controllo. |
| `Author` | Stringa | `Morgan / NewSoft` | Nome dell'autore o designer del tema. |
| `Version` | Stringa | `1.0` | Versione del tema. |
| `StyleEngine` | Stringa | `Classic`, `FluentGlass`, `MicaLight`, `MicaDark` | Identificatore del motore di rendering grafico associato: <br>• `Classic` (ID 0): rilievo 3D Windows 2000 squadrato.<br>• `FluentGlass` (ID 1): vetro satinato frosted glass con sfocatura wallpaper.<br>• `MicaLight` (ID 2): finitura acrilica Mica chiara semi-opaca.<br>• `MicaDark` (ID 3): finitura acrilica Mica scura semi-opaca per ambienti notturni. |
| `Description` | Stringa | `Tema raffinato in materiale Mica...` | Descrizione visualizzata nei dettagli del tema. |

---

### 3.2. Sezione `[Metrics]` — Dimensioni e Proprietà Geometriche

| Chiave | Tipo | Default | Descrizione |
|---|---|---|---|
| `CornerRadius` / `WindowRadius` | Intero | `8` | Raggio di curvatura degli angoli esterni della finestra (in pixel). `0` per angoli retti stile classico Windows 2000, `8` per stile Fluent/Mica, `10-12` per Liquid Glass. |
| `ButtonRadius` | Intero | `ControlRadius` | Raggio di curvatura per i pulsanti (toolbar, dialoghi OK/Annulla, azioni laterali). Se omesso, eredita `ControlRadius`. |
| `TabRadius` | Intero | `ControlRadius` | Raggio di curvatura per le schede e tab di navigazione. Se omesso, eredita `ControlRadius`. |
| `InputRadius` | Intero | `ControlRadius` | Raggio di curvatura per caselle di testo, barra degli indirizzi e campi di ricerca. Se omesso, eredita `ControlRadius`. |
| `PanelRadius` | Intero | `ControlRadius+2` | Raggio di curvatura per pannelli contenitori, riquadri web view laterali e griglia icone. Se omesso, eredita `ControlRadius + 2` (default `8`). |
| `StartBtnRadius` | Intero | `ControlRadius+2` | Raggio di curvatura per il pulsante Start sulla barra delle applicazioni (default `8`). Se omesso, eredita `ControlRadius + 2`. |
| `TaskbarBtnRadius` / `TaskbarRadius` | Intero | `ControlRadius` | Raggio di curvatura per i pulsanti delle finestre aperte sulla barra delle applicazioni (default `6`). Se omesso, eredita `ControlRadius`. |
| `TrayRadius` | Intero | `ControlRadius` | Raggio di curvatura per la pillola dell'orologio e del vassoio di sistema (system tray, default `6`). Se omesso, eredita `ControlRadius`. |
| `ControlRadius` | Intero | `4` | Raggio di curvatura generale predefinito per tutti i controlli interni (usato come fallback universale se un controllo specifico non è dichiarato). |
| `TitleHeight` | Intero | `22` | Altezza della barra del titolo (in pixel). |
| `BorderWidth` | Intero | `1` | Spessore del contorno esterno della finestra (in pixel). |
| `DropShadow` | Booleano | `1` | `1` per attivare l'ombra diffusa dinamica sotto le finestre, `0` per disattivarla. |
| `ShadowSpread` | Intero | `6` | Estensione in pixel dell'ombra morbida multi-layer (es. 4, 6, 8 pixel). |
| `SpecularRim` | Booleano | `0` | `1` per bordo brillante con riflesso speculare 3D; `0` per rendering completamente flat moderno senza effetti 3D (consigliato per Windows 11 Fluent). |
| `ModernControls` | Booleano | `1` | `1` per pulsanti, schede e box arrotondati e moderni, `0` per controlli con effetto bevel 3D classico a rilievo. |
| `ControlBorder` | Booleano | `1` | `1` per disegnare un sottile contorno di contrasto attorno a pulsanti e controlli, `0` altrimenti. |

---

### 3.3. Sezione `[Colors]` — Palette Cromatica e Trasparenze

| Chiave | Tipo | Valori Esempio | Descrizione |
|---|---|---|---|
| `DesktopColor` | Colore | `0x1E2A38` | Colore base a tinta unita del desktop (utilizzato se non è presente un wallpaper). |
| `SurfaceBase` | Colore | `0xF8F9FA` | Colore di base della superficie e del corpo finestra. |
| `SurfaceAlpha` | Alpha | `235` | Livello di opacità del corpo finestra (da `0` a `255`). <br>• `< 200`: Vetro Frosted Glass ad alta trasparenza (campiona e sfoca il desktop).<br>• `220 - 245`: Finitura acrilica Mica semi-opaca (92%).<br>• `255`: Superficie completamente solida opaca. |
| `ActiveTitle` | Colore | `0xF8F9FA` | Colore di tint o sfondo della barra del titolo per la finestra **attiva** (in primo piano). |
| `ActiveTitleText` | Colore | `0x1F1F1F` | Colore del testo del titolo per la finestra **attiva**. |
| `InactiveTitle` | Colore | `0xEAECEF` | Colore di sfondo della barra del titolo per la finestra **inattiva** (in secondo piano). |
| `InactiveTitleText` | Colore | `0x757575` | Colore del testo del titolo per la finestra **inattiva**. |
| `WindowBg` | Colore | `0xFFFFFF` | Colore di riempimento dell'area di lavoro client delle applicazioni. |
| `WindowText` | Colore | `0x1E1E1E` | Colore del testo standard per il contenuto dei documenti. |
| `BtnFace` | Colore | `0xF0F2F5` | Colore di sfondo dei pulsanti standard a riposo. |
| `BtnText` | Colore | `0x1E1E1E` | Colore del testo dei pulsanti a riposo. |
| `BtnHover` | Colore | `0xE4E6EB` | Colore di sfondo dei pulsanti al passaggio del mouse o al fuoco. |
| `BorderColor` | Colore | `0xE2E2E2` | Colore del contorno perimetrale per la finestra attiva. |
| `BorderInactive` | Colore | `0xD5D5D5` | Colore del contorno perimetrale per le finestre inattive. |
| `ControlBorderColor` | Colore | `0xD8D8D8` | Colore della bordatura perimetrale dei controlli (pulsanti, schede). |
| `AccentColor` | Colore | `0x0078D4` | Colore di accento primario del sistema (utilizzato per evidenziazioni, tab selezionate e pulsanti premuti). |
| `AccentText` | Colore | `0xFFFFFF` | Colore del testo quando posizionato sopra lo sfondo con `AccentColor`. |
| `SelectionColor` | Colore | `0xCCE8FF` | Colore di selezione per elementi di liste o testo evidenziato. |
| `SelectionText` | Colore | `0x004578` | Colore del testo selezionato. |
| `CloseHoverBg` | Colore | `0xE81123` | Colore di sfondo per il pulsante Chiudi [X] (es. rosso Windows 11). |

---

### 3.4. Sezione `[Taskbar]` — Barra delle Applicazioni e System Tray

| Chiave | Tipo | Valori Esempio | Descrizione |
|---|---|---|---|
| `TaskbarColor` | Colore | `0xF3F3F7` | Colore di base della barra delle applicazioni inferiore. |
| `TaskbarAlpha` | Alpha | `235` | Grado di trasparenza della barra delle applicazioni (`0-255`). |
| `TaskbarHeight` | Intero | `28` | Altezza in pixel della barra delle applicazioni (default: 28px). |
| `TaskbarBorder` | Colore | `0x000000` | Colore della linea divisoria superiore tra desktop e taskbar. |
| `StartBtnColor` | Colore | `0xE8E8EE` | Colore di sfondo della capsula/pulsante "Start". |
| `StartBtnText` | Colore | `0x1E1E1E` | Colore della scritta "Start". |
| `ActiveTabIndicator` | Colore | `0x0078D4` | Colore della barretta di evidenziazione della finestra attiva sulla taskbar. |
| `TrayPillColor` | Colore | `0xEAEAEF` | Colore della pillola arrotondata nella System Tray (orologio CMOS ed indicatori). |

---

### 3.5. Sezione `[Wallpaper]` — Sfondo del Desktop

| Chiave | Tipo | Valori Esempio | Descrizione |
|---|---|---|---|
| `File` | Stringa | `C:\NewOS\Resource\wallpapers\FLOW.BMP` | Percorso assoluto dell'immagine bitmap o JPEG (`none` se a tinta unita). |
| `Style` | Stringa | `Stretch`, `Center`, `Tile` | Modalità di ridimensionamento: <br>• `Stretch`: adatta l'immagine alla risoluzione corrente.<br>• `Center`: centra l'immagine senza deformarla.<br>• `Tile`: ripete l'immagine a mosaico. |

---

### 3.6. Sezione `[Sounds]` — Eventi Sonori di Sistema

| Chiave | Tipo | Valori Esempio | Descrizione |
|---|---|---|---|
| `Startup` | Stringa | `C:\NewOS\Resource\sounds\STARTUP.WAV` | File audio PCM WAV a 16-bit riprodotto al boot del sistema. |
| `Click` | Stringa | `C:\NewOS\Resource\sounds\CLICK.WAV` | File audio riprodotto al click sui controlli o all'apertura finestre. |

---

## 4. Esempi Ufficiali Preconfigurati

### Esempio 1: Windows 11 Fluent Glass Light (Alta Trasparenza)
File: [`GLASS_LIGHT.THEMES`](file:///c:/Users/morga/Desktop/MyOS/SystemPackFileWach/NewOS/Resource/Themes/GLASS_LIGHT.THEMES)

```ini
[Theme]
Name=Windows 11 Fluent Glass Light
Author=Morgan
Version=1.0
StyleEngine=FluentGlass
Description=Vetro satinato frosted glass ad alta trasparenza (65%) con rifrazione del wallpaper e testi ad alto contrasto

[Metrics]
CornerRadius=8
TitleHeight=22
BorderWidth=1
DropShadow=1
ShadowSpread=8
SpecularRim=1
ModernControls=1
ControlRadius=6
ControlBorder=1

[Colors]
DesktopColor=0x1E2A38
SurfaceBase=0xFFFFFF
SurfaceAlpha=165
ActiveTitle=0x4080FF
ActiveTitleText=0xFFFFFF
InactiveTitle=0x808090
InactiveTitleText=0x666666
WindowBg=0xFFFFFF
WindowText=0x1A1A1A
BtnFace=0x404040
BtnText=0xFFFFFF
BtnHover=0x505050
BorderColor=0xE8E8E8
BorderInactive=0xD0D0D0
ControlBorderColor=0xFFFFFF
AccentColor=0x0078D4
AccentText=0xFFFFFF
SelectionColor=0xB8DDFF
SelectionText=0x003366
CloseHoverBg=0xFF3030

[Taskbar]
TaskbarColor=0x08080F
TaskbarAlpha=160
TaskbarHeight=28
TaskbarBorder=0x303040
StartBtnColor=0x3060CC
StartBtnText=0xFFFFFF
ActiveTabIndicator=0x0078D4
TrayPillColor=0x101020

[Wallpaper]
File=C:\NewOS\Resource\wallpapers\FLOW.BMP
Style=Stretch

[Sounds]
Startup=C:\NewOS\Resource\sounds\STARTUP.WAV
Click=C:\NewOS\Resource\sounds\CLICK.WAV
```

---

### Esempio 2: Windows 11 Mica Light (Semi-Opaco Acrilico)
File: [`MICA_LIGHT.THEMES`](file:///c:/Users/morga/Desktop/MyOS/SystemPackFileWach/NewOS/Resource/Themes/MICA_LIGHT.THEMES)

```ini
[Theme]
Name=Windows 11 Mica Light
Author=Morgan
Version=1.0
StyleEngine=MicaLight
Description=Tema chiaro raffinato in materiale Mica semi-opaco (92%) con campionamento del colore d'ambiente

[Metrics]
CornerRadius=8
TitleHeight=22
BorderWidth=1
DropShadow=1
ShadowSpread=6
SpecularRim=1
ModernControls=1
ControlRadius=4
ControlBorder=1

[Colors]
DesktopColor=0x1E2A38
SurfaceBase=0xF8F9FA
SurfaceAlpha=235
ActiveTitle=0xF8F9FA
ActiveTitleText=0x1F1F1F
InactiveTitle=0xEAECEF
InactiveTitleText=0x757575
WindowBg=0xFFFFFF
WindowText=0x1E1E1E
BtnFace=0xF0F2F5
BtnText=0x1E1E1E
BtnHover=0xE4E6EB
BorderColor=0xE2E2E2
BorderInactive=0xD5D5D5
ControlBorderColor=0xD8D8D8
AccentColor=0x0078D4
AccentText=0xFFFFFF
SelectionColor=0xCCE8FF
SelectionText=0x004578
CloseHoverBg=0xE81123

[Taskbar]
TaskbarColor=0xF3F3F7
TaskbarAlpha=235
TaskbarHeight=28
TaskbarBorder=0x000000
StartBtnColor=0xE8E8EE
StartBtnText=0x1E1E1E
ActiveTabIndicator=0x0078D4
TrayPillColor=0xEAEAEF

[Wallpaper]
File=C:\NewOS\Resource\wallpapers\FLOW.BMP
Style=Stretch

[Sounds]
Startup=C:\NewOS\Resource\sounds\STARTUP.WAV
Click=C:\NewOS\Resource\sounds\CLICK.WAV
```

---

### Esempio 3: Windows 11 Mica Dark (Dark Mode Notturno)
File: [`MICA_DARK.THEMES`](file:///c:/Users/morga/Desktop/MyOS/SystemPackFileWach/NewOS/Resource/Themes/MICA_DARK.THEMES)

```ini
[Theme]
Name=Windows 11 Mica Dark
Author=Morgan
Version=1.0
StyleEngine=MicaDark
Description=Tema scuro moderno basato su materiale Mica e Fluent Design con riflessi cromatici dal wallpaper

[Metrics]
CornerRadius=8
TitleHeight=22
BorderWidth=1
DropShadow=1
ShadowSpread=8
SpecularRim=1
ModernControls=1
ControlRadius=4
ControlBorder=1

[Colors]
DesktopColor=0x0B0F19
SurfaceBase=0x202025
SurfaceAlpha=225
ActiveTitle=0x202025
ActiveTitleText=0xFFFFFF
InactiveTitle=0x18181C
InactiveTitleText=0x8A8A8E
WindowBg=0x1C1C21
WindowText=0xE2E8F0
BtnFace=0x2A2A32
BtnText=0xFFFFFF
BtnHover=0x383842
BorderColor=0x383838
BorderInactive=0x28282E
ControlBorderColor=0x44444C
AccentColor=0x0078D4
AccentText=0xFFFFFF
SelectionColor=0x0078D4
SelectionText=0xFFFFFF
CloseHoverBg=0xC42B1C

[Taskbar]
TaskbarColor=0x16161B
TaskbarAlpha=215
TaskbarHeight=28
TaskbarBorder=0x2E2E36
StartBtnColor=0x2A2A32
StartBtnText=0xFFFFFF
ActiveTabIndicator=0x0078D4
TrayPillColor=0x24242A

[Wallpaper]
File=C:\NewOS\Resource\wallpapers\FLOW.BMP
Style=Stretch

[Sounds]
Startup=C:\NewOS\Resource\sounds\STARTUP.WAV
Click=C:\NewOS\Resource\sounds\CLICK.WAV
```

---

### Esempio 4: Windows 2000 Classic Teal (Interfaccia 3D Beveled Solida)
File: [`TEAL_CLASSIC.THEMES`](file:///c:/Users/morga/Desktop/MyOS/SystemPackFileWach/NewOS/Resource/Themes/TEAL_CLASSIC.THEMES)

```ini
[Theme]
Name=Windows 2000 Classic Teal
Author=Morgan
Version=1.0
StyleEngine=Classic
Description=Interfaccia classica a rilievo 3D con barra blu cobalto e desktop verde acqua (autolive_screen.png)

[Metrics]
CornerRadius=0
TitleHeight=20
BorderWidth=3
DropShadow=0
ShadowSpread=0
SpecularRim=0
ModernControls=0
ControlRadius=0
ControlBorder=1

[Colors]
DesktopColor=0x008080
SurfaceBase=0xD4D0C8
SurfaceAlpha=255
ActiveTitle=0x0A246A
ActiveTitleText=0xFFFFFF
InactiveTitle=0x808080
InactiveTitleText=0xD4D0C8
WindowBg=0xFFFFFF
WindowText=0x000000
BtnFace=0xD4D0C8
BtnText=0x000000
BtnHover=0xE0DCD4
BorderColor=0x808080
BorderInactive=0x808080
ControlBorderColor=0x404040
AccentColor=0x0A246A
AccentText=0xFFFFFF
SelectionColor=0x0A246A
SelectionText=0xFFFFFF
CloseHoverBg=0xD4D0C8

[Taskbar]
TaskbarColor=0xD4D0C8
TaskbarAlpha=255
TaskbarHeight=28
TaskbarBorder=0xFFFFFF
StartBtnColor=0xD4D0C8
StartBtnText=0x000000
ActiveTabIndicator=0x000000
TrayPillColor=0xD4D0C8

[Wallpaper]
File=none
Style=Center

[Sounds]
Startup=C:\NewOS\Resource\sounds\STARTUP.WAV
Click=C:\NewOS\Resource\sounds\CLICK.WAV
```

---

## 5. API C per Programmatori e Sviluppatori Applicazioni

Gli sviluppatori di applicazioni in NewOS V1 possono accedere alle proprietà grafiche attive e disegnare controlli coerenti utilizzando le API esposte in [`kernel/gui/wm.h`](file:///c:/Users/morga/Desktop/MyOS/kernel/gui/wm.h):

### 5.1. Lettura dello Stato del Tema
```c
// Restituisce il puntatore alle proprietà grafiche del tema attualmente applicato
const theme_info_t* cur_theme = wm_get_current_theme_props();
if (cur_theme) {
    uint32_t bg_color     = cur_theme->window_bg;
    uint32_t text_color   = cur_theme->window_text;
    uint32_t accent_color = cur_theme->accent_color;
    bool is_modern        = cur_theme->modern_controls;
}
```

### 5.2. Disegno di Pulsanti a Tema
```c
// Disegna un pulsante conforme al tema attivo (arrotondato o beveled 3D)
wm_draw_themed_button(x, y, width, height, "Salva", is_pressed);
```

### 5.3. Disegno di Schede (Tabs) a Tema
```c
// Disegna una scheda con indicatore di accento inferiore o tab classica
wm_draw_themed_tab(x, y, width, height, "Proprietà", is_active);
```

### 5.4. Applicazione Programmatica di un Tema
```c
// Applica un tema per indice dalla libreria THEMEMGR.DLL
theme_mgr_apply(theme_index);
```
