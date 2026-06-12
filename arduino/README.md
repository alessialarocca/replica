# Replica_ — Arduino Nano ESP32 Setup

## Librerie richieste (Arduino IDE → Library Manager)

- **GxEPD2** (ZinggJM) — driver per i display Waveshare e-ink
- **Adafruit GFX** — primitivi grafici e font usati da GxEPD2
- **ArduinoJson** v6 (Benoit Blanchon)
- **WiFi** — inclusa nel core Nano ESP32, non richiede installazione

## Board Manager

1. File → Preferences → Additional Boards URLs:
   `https://downloads.arduino.cc/packages/package_index.json`
2. Tools → Board → Boards Manager → cerca **Arduino Nano ESP32** → Install

## Wiring

### Encoder 1 — selezione categoria + click azione
```
CLK   →  D2
DT    →  D3
SW    →  D4
+     →  3.3V
GND   →  GND
```
Rotazione: cicla le 6 categorie (BIO → GEO → PROF → ECON → SOCIO → PSYCHO → BIO …).
Click (SW): "spara" l'azione corrente sulla categoria selezionata.

### Encoder 2 — modo azione (senza click)
```
CLK   →  D5
DT    →  D6
+     →  3.3V
GND   →  GND
```
Rotazione clampata a 3 stati:
- CCW max = **POISON**
- centro  = **NEUTRAL** (nessuna azione anche se l'encoder 1 viene cliccato)
- CW max  = **AMPLIFY**

Il pin SW dell'encoder 2 può restare scollegato — non viene letto.

Tutti i pin degli encoder usano `INPUT_PULLUP` interno.

### LED RGB 4 pin (R, G, B + common)
```
R     →  A0   (con resistore ~220Ω in serie)
G     →  A1   (con resistore ~220Ω in serie)
B     →  A2   (con resistore ~220Ω in serie)
common (pin lungo) → GND   se LED common-cathode (default)
                  → 3.3V  se LED common-anode
```
Se il tuo LED è common-anode, apri `replica_firmware.ino` e imposta:
```cpp
const bool LED_COMMON_ANODE = true;
```
così la libreria inverte i livelli PWM (255 = spento, 0 = pieno).

Comportamento del LED:
- **ARANCIONE** → encoder 2 in posizione POISON
- **VERDE** → encoder 2 in posizione AMPLIFY
- **SPENTO** → encoder 2 in posizione NEUTRAL

Quando una nuova categoria viene decontestualizzata, il LED fa **due
lampi viola** (≈1 secondo totale) e poi torna al colore di regime
definito dall'encoder 2. Non resta viola — è solo un segnale di evento.

Sul display non viene più mostrato nessun header / strip / blink: la
schermata è dedicata alla sola frase, con la parola della categoria
selezionata (encoder 1) riquadrata in nero.

### Waveshare e-ink 2.13" B/W (SPI hardware)
```
VCC   →  5V
GND   →  GND
DIN   →  D11  (MOSI)
CLK   →  D13  (SCK)
CS    →  D10
DC    →   D9
RST   →   D8
BUSY  →   D7
```

### Modello del display

Il firmware è impostato sul **Waveshare 2.13" V3 B/W (250×122)** —
classe `GxEPD2_213_B74` della libreria GxEPD2.

Se hai una revisione diversa del 2.13", apri
`replica_firmware/replica_firmware.ino` e sostituisci entrambe le
occorrenze di `GxEPD2_213_B74` con la classe corrispondente:

| Modello Waveshare           | Risoluzione | Classe GxEPD2          |
|-----------------------------|-------------|------------------------|
| 2.13" V1 (originale)        | 250×122     | `GxEPD2_213`           |
| 2.13" V2                    | 250×122     | `GxEPD2_213_B72`       |
| 2.13" V2.1                  | 250×122     | `GxEPD2_213_B73`       |
| 2.13" V3 (default)          | 250×122     | `GxEPD2_213_B74`       |
| 2.13" 4-grayscale           | 250×122     | `GxEPD2_213_M21`       |
| 2.13" "BN" / nuovo          | 250×122     | `GxEPD2_213_BN`        |

Tutte le varianti usano lo stesso wiring sopra.

### Alimentazione: LiPo + caricatore USB-C + interruttore ON/OFF

Per rendere il device autonomo (non legato al cavo USB-C del PC) servono
tre componenti:

| Componente | Esempi compatibili | Note |
|------------|--------------------|------|
| **Caricatore USB-C all-in-one** | Adafruit PowerBoost 500/1000 Charger, DFRobot DFR0264, Pimoroni LiPo SHIM USB-C, moduli generici "TP4056 USB-C + boost 5 V" | Deve avere SIA charger LiPo SIA boost 5 V integrato. Se prendi un TP4056 nudo serve uno step-up esterno (MT3608) |
| **Batteria LiPo singola cella 3.7 V** | 500 – 2000 mAh, connettore JST PH 2.0 mm | ≥1000 mAh consigliato: il Nano ESP32 con WiFi attivo tira picchi >300 mA |
| **Interruttore SPST toggle/slide** | Qualsiasi switch da 1 A | Va in serie sul **5 V in uscita** del caricatore, NON sul filo della batteria nuda |

**Schema di collegamento**

```
USB-C  (5 V in)
   │
   ▼
┌──────────────────────────────┐
│  CARICATORE USB-C + BOOST 5V │   ← carica la batteria quando l'USB-C è
│                              │     collegato, eroga 5 V dalla LiPo
│  BAT+   BAT-      OUT+  GND  │     quando l'USB-C è staccato
└───┬──────┬─────────┬─────┬───┘
    │      │         │     │
   LiPo+  LiPo-      │     │
                     │     │
              SW ────┤     │   ← interruttore ON/OFF in serie sul 5 V
                     │     │
                     ▼     ▼
                    Vin   GND   del Nano ESP32
```

**Regole importanti**

- L'interruttore va **dopo** lo stadio boost del caricatore (sul filo
  che entra in `Vin`), **mai** direttamente sui poli della batteria
  nuda: bypasseresti la protezione della cella.
- Il caricatore alimenta SIA il Nano SIA la batteria mentre l'USB-C è
  collegato → si può usare e caricare contemporaneamente.
- Se il modulo che hai NON ha un boost interno (es. TP4056 base), serve
  uno step-up esterno tipo MT3608 tra `OUT` (3.7 – 4.2 V) e `Vin` del
  Nano, che richiede ≥4.5 V su `Vin`.
- Autonomia indicativa con LiPo 1000 mAh, WiFi attivo e display in idle:
  3 – 4 ore. Quando il LED del caricatore segnala batteria scarica,
  ricaricala collegando l'USB-C.

## Configurazione

Apri `replica_firmware.ino` e modifica le prime due righe:
```cpp
const char* WIFI_SSID     = "nome-rete";
const char* WIFI_PASSWORD = "password";
```

## Flash e primo avvio

1. Collega il Nano ESP32 via USB-C
2. Tools → Board → Arduino Nano ESP32
3. Tools → Port → (seleziona la porta COM/tty del device)
4. Upload (freccia →)
5. All'avvio il display mostra l'IP assegnato (es. `192.168.1.42`)

> ⚠ Gli e-ink hanno refresh lento (~2s per full refresh). Il firmware fa
> un **full refresh** solo quando arriva una nuova frase, mentre il cambio
> di posizione dello switch usa un **partial refresh** dell'header per
> rispondere quasi istantaneamente. Dopo molti partial refresh consecutivi
> potrebbe apparire del ghosting: invia una nuova frase per pulire.

## Collegamento con l'estensione

1. Apri il popup dell'estensione Chrome
2. Nella sezione **Physical device** in basso, inserisci l'IP mostrato sul display
3. Premi **Connect**
4. Il dot verde conferma la connessione

Da quel momento:
- Sul display compare solo la frase algoritmica. La parola della
  categoria selezionata viene riquadrata in nero (testo bianco).
- Encoder 1 → ruota la selezione fra le 6 categorie (la cornice nera
  si sposta sulla parola corrispondente)
- Encoder 2 → cambia il modo di azione (visibile solo sul LED:
  arancione, verde, spento)
- Click sull'encoder 1 → applica il modo corrente alla categoria
  selezionata (se NEUTRAL il click non ha effetto)
- Quando il webapp rileva una nuova decontestualizzazione, il LED
  fa due lampi viola e poi torna al colore di regime
- La webapp su localhost si aggiorna automaticamente (polling 2s)

## Endpoint HTTP esposti dal device

| Metodo | Path        | Descrizione                                                                                                  |
|--------|-------------|--------------------------------------------------------------------------------------------------------------|
| GET    | `/status`   | Ping + stato corrente (`action`, `category`, `decontext`, `ip`)                                              |
| POST   | `/sentence` | Body `{ "text": "...", "decontext": ["WORD1", …], "vocables": { "bio": "...", … } }` → aggiorna display + LED |
| GET    | `/action`   | `{ action, category, fresh }` — `fresh=true` significa click pendente                                        |
