// ─────────────────────────────────────────────────────────────
// REPLICA_ — Arduino Nano ESP32 firmware v2.2
// HTTP server: riceve la frase + parole decontestualizzate +
// mappa categoria→vocabolo dall'estensione Chrome. Sul display
// e-ink mostra SOLO la frase: la parola della categoria
// selezionata (encoder 1) viene riquadrata in nero. Il LED
// segnala il modo (encoder 2). Su ogni nuova decontestualizzazione
// il LED fa due lampi arancioni e poi torna al colore del modo.
//
// Mappatura LED a regime:
//   PURPLE → modo POISON
//   GREEN  → modo AMPLIFY
//   OFF    → modo NEUTRAL
// Su nuova decontestualizzazione → 2 lampi ORANGE → ritorno a regime.
//
// Librerie richieste (Library Manager):
//   - GxEPD2 (ZinggJM)              — driver Waveshare e-ink
//   - Adafruit GFX                  — primitivi grafici / font
//   - ArduinoJson (v6, Benoit Blanchon)
//   - WiFi (inclusa nel core Nano ESP32)
//
// Wiring encoder 1 (CON click):
//   CLK   →  D2
//   DT    →  D3
//   SW    →  D4
//   +     →  3.3V
//   GND   →  GND
//
// Wiring encoder 2 (rotazione SOLO, click non collegato):
//   CLK   →  D5
//   DT    →  D6
//   +     →  3.3V
//   GND   →  GND
//
// Wiring LED RGB 4 pin (common cathode di default):
//   R     →  A0  (con resistore ~220Ω in serie)
//   G     →  A1  (con resistore ~220Ω in serie)
//   B     →  A2  (con resistore ~220Ω in serie)
//   GND   →  GND (pin più lungo, se common-cathode)
//
// Se hai un LED common-anode, collega il pin lungo a 3.3V e metti
// LED_COMMON_ANODE = true nella sezione "Pin" qui sotto.
//
// Wiring Waveshare e-ink 2.13" B/W (SPI hardware):
//   VCC   →  5V
//   GND   →  GND
//   DIN   →  D11  (MOSI)
//   CLK   →  D13  (SCK)
//   CS    →  D10
//   DC    →   D9
//   RST   →   D8
//   BUSY  →   D7
//
// ── Alimentazione: LiPo + caricatore USB-C + interruttore ────
// Schema consigliato (modulo all-in-one tipo Adafruit PowerBoost
// 500/1000 Charger, DFRobot DFR0264, Pimoroni LiPo SHIM USB-C, o
// qualsiasi modulo "TP4056 USB-C + boost 5V"):
//
//   USB-C (5 V in)
//        │
//        ▼
//   ┌──────────────────────────┐
//   │  CARICATORE USB-C +      │   ← carica la batteria quando
//   │  BOOST 5 V               │     il cavo USB-C è inserito,
//   │                          │     altrimenti eroga 5 V dalla LiPo
//   │  BAT+   BAT-   OUT+ GND  │
//   └───┬──────┬──────┬────┬───┘
//       │      │      │    │
//      LiPo+  LiPo-   │    │
//                     │    │
//                  SW ┤    │   ← SPST toggle switch ON/OFF in serie
//                     │    │     sul positivo (interrompe Vin del Nano)
//                     ▼    ▼
//                  Vin    GND   del Nano ESP32
//
//   NOTE
//   - LiPo singola cella 3.7 V, capacità a piacere (≥500 mAh per
//     un paio d'ore di autonomia con WiFi attivo).
//   - L'interruttore va SEMPRE dopo il boost 5 V, mai sul filo
//     della batteria nuda (rischio di lasciare la cella a metà
//     scarica senza protezione).
//   - Se il modulo NON ha un boost interno (es. TP4056 nudo),
//     serve uno step-up esterno tipo MT3608 tra OUT (3.7-4.2 V)
//     e Vin del Nano.
//   - Il caricatore alimenta SIA il Nano SIA la batteria mentre
//     l'USB-C è collegata, quindi puoi lasciarlo in carica e usarlo
//     contemporaneamente.
// ─────────────────────────────────────────────────────────────

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <GxEPD2_BW.h>
// Frase: IBM Plex Mono 6pt (generato con fontconvert, cap-height 7 px,
// cell 7x8). Taglia intermedia fra built-in (8 px) e FreeMono9pt7b (13 px).
// Command line e boot screen: bitmap built-in 5x7 (cell 6x8 a setTextSize(1)).
#include "IBMPlexMono6pt7b.h"
#include <ArduinoJson.h>
#include <SPI.h>

// ── Font ─────────────────────────────────────────────────────
// Frase: IBM Plex Mono 6pt — monospace, cap-height 7 px, cell 7x8.
// Generato da IBMPlexMono-Regular.ttf con fontconvert (Adafruit GFX).
// Sta fra il bitmap built-in 5x7 (8 px, troppo piccolo) e FreeMono9pt7b
// (13 px, troppo grande).
// Command line e boot screen: bitmap built-in 5x7 (cell 6x8, setTextSize 1).

// ── Credenziali WiFi ─────────────────────────────────────────
// Nano ESP32 supporta solo 2.4 GHz. Se l'hotspot del telefono pubblica
// su 5 GHz (di default su molti iPhone) la connessione NON parte.
// Controllare: hotspot iPhone → "Maximize Compatibility" = ON.
const char* WIFI_SSID     = "TIMDAISY";
const char* WIFI_PASSWORD = "Kobe2019!";

// Quanto spesso ricontrolliamo lo stato WiFi nel loop principale.
// Se il link cade, riproviamo automaticamente con un ciclo non bloccante.
static const unsigned long WIFI_CHECK_INTERVAL_MS  = 5000;
// Quanto a lungo aspettiamo un singolo tentativo di bring-up (blocking).
static const unsigned long WIFI_BRINGUP_TIMEOUT_MS = 35000;
static unsigned long s_lastWifiCheckMs = 0;
static bool          s_wifiUp          = false;

// ── Pin ──────────────────────────────────────────────────────
#define ENC1_CLK  2
#define ENC1_DT   3
#define ENC1_SW   4
#define ENC2_CLK  5
#define ENC2_DT   6

#define EPD_CS    10
#define EPD_DC     9
#define EPD_RST    8
#define EPD_BUSY   7

#define LED_R     A0
#define LED_G     A1
#define LED_B     A2
const bool LED_COMMON_ANODE = false;

// ── Display: Waveshare 2.13" V3 B/W (250x122) ────────────────
GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(
  GxEPD2_213_B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

const int SCREEN_W = 250;
const int SCREEN_H = 122;
const int CMD_H    = 18;            // altezza area command line in basso
const int SENT_H   = SCREEN_H - CMD_H - 2;  // 102 px per la frase

// Intensità (encoder 1 in action state cicla qui)
const char* INT_LABELS[] = { "LOW", "MID", "HIGH" };
const int   N_INT        = 3;

// ── LED RGB (3 canali PWM) ───────────────────────────────────
struct RGB { uint8_t r, g, b; };
// Compensazione percettiva: il diodo verde è più "efficiente" all'occhio
// umano dei rossi/blu, quindi a parità di PWM appare più brillante.
// Alziamo R e B su ORANGE e PURPLE per pareggiare la luminosità percepita
// di GREEN. (Se l'orange tende troppo al rosso o il purple al blu, regola
// il rapporto fra i canali — il PWM totale è già nel range "luminoso".)
const RGB COL_PURPLE = { 140,   0, 255 };  // viola intenso
// Verde è molto più "efficiente" del rosso sull'LED: con G=55 il blink
// di decontestualizzazione viene letto come verde. Abbassiamo G quasi a 0
// per ottenere un arancione che NON sconfini nel giallo/verde.
const RGB COL_ORANGE = { 255,  20,   0 };  // arancio caldo (rosso dominante)
const RGB COL_GREEN  = {   0,  90,  20 };  // riferimento
const RGB COL_OFF    = {   0,   0,   0 };

// ── Server HTTP ──────────────────────────────────────────────
WebServer server(80);

// ── Categorie (devono combaciare con CAT_ORDER nel webapp) ───
const char* CAT_KEYS[]   = { "bio",  "geo",  "prof",  "econ", "socio", "psycho" };
// Nome esteso mostrato nella info line — combacia con CAT_LABELS lato
// estensione (popup.js). Sono già in maiuscolo per il print() del bitmap.
const char* CAT_FULL_NAMES[] = {
  "BIO-DEMOGRAPHIC",
  "GEOGRAPHIC",
  "PROFESSIONAL",
  "ECONOMIC",
  "SOCIO-CULTURAL",
  "PSYCHO-BEHAVIOURAL"
};
const int   N_CAT        = 6;

// Posizione di ciascuna categoria nella frase costruita dall'estensione:
// "IDENTIFIED AS [bio], LOCATED IN [geo], WORKING AS [prof],
//  VALUED AS [econ], NETWORKED WITHIN [socio], AND EXHIBITING [psycho]."
// Ora l'ordine nella frase combacia con CAT_KEYS → mappa identità.
// (Serve a disambiguare quale "[?]" evidenziare quando il vocabolo della
//  categoria selezionata è vuoto e ce ne sono altre vuote nella frase.)
const int CAT_SENTENCE_POS[N_CAT] = { 0, 1, 2, 3, 4, 5 };

// ── Stato globale ────────────────────────────────────────────
String currentSentence = "AWAITING\nCONNECTION...";

// Vocabolo corrente per ogni categoria (uppercase). Vuoto = nessun dato.
String catVocables[N_CAT];

// Encoder 1 — indice categoria selezionata (0..N_CAT-1)
volatile int  enc1Pos = 0;
// Stato del decoder quadratura per encoder 1 (specchia quello di enc2).
volatile uint8_t enc1PrevState = 0;
volatile int8_t  enc1SubStep   = 0;
// Detent emessi dall'ISR e ancora da consumare nel loop (signed, +CW/-CCW).
volatile int     enc1PendingDelta = 0;
// Timeout selezione categoria: se enc1 non si muove per N ms, la selezione
// scompare dalla frase. Ricompare al primo click successivo.
const unsigned long ENC1_SELECTION_TIMEOUT_MS = 10000;
unsigned long lastEnc1MoveMs = 0;
// Encoder 2 — stato azione: -1 = poison, 0 = neutral, +1 = amplify
volatile int  enc2Pos = 0;
// Encoder 2 — posizione assoluta corrente, integer in [0..ENC2_POS_MAX].
// L'ISR scrive direttamente qui. HARD-STOP a entrambi gli estremi:
// ruotare oltre pos 0 o oltre pos 10 NON ha effetto (niente wrap).
//
// Mappa posizione → modo (NEUTRAL al centro):
//   pos  0..3  → amplify (+1)   ← fondo corsa CCW
//   pos  4..6  → neutral ( 0)   ← centro, boot default
//   pos  7..10 → poison  (-1)   ← fondo corsa CW
const int ENC2_POS_MAX        = 10;  // posizione massima inclusiva
const int ENC2_NEUTRAL_START  = 4;   // pos < 4  → amplify
const int ENC2_POISON_START   = 7;   // pos < 7  → neutral, altrimenti poison
const int ENC2_BOOT_POS       = 5;   // partenza al centro (neutral)
volatile int enc2EncPos = ENC2_BOOT_POS;

// ── Decoder a quadratura per encoder 2 ──────────────────────
// Tabella delle transizioni di stato: indicizzata da (prevState<<2)|currState
// dove state = (CLK<<1)|DT. Valori validi: +1 (CW), -1 (CCW), 0 (rimbalzo
// o transizione invalida → ignorata). Questa è l'unica difesa necessaria
// contro il bounce dei contatti: i pattern non validi semplicemente non
// emettono un sub-step e non spostano la posizione.
const int8_t ENC2_QUAD_TABLE[16] = {
   0, +1, -1,  0,
  -1,  0,  0, +1,
  +1,  0,  0, -1,
   0, -1, +1,  0
};
// Un detent fisico = 4 transizioni complete in quadratura (encoder
// EC11 standard). Quando l'accumulatore raggiunge ±4, emettiamo
// esattamente 1 posizione (clamped da hard-stop).
const int ENC2_TRANSITIONS_PER_DETENT = 4;
volatile uint8_t enc2PrevState = 0;   // ultimo (CLK<<1)|DT letto
volatile int8_t  enc2SubStep   = 0;   // accumulatore in [-3..+3]
// Click encoder 1 → segnala azione "fresca" da consumare via /action
volatile bool actionFresh = false;

// Snapshot scollegato dai volatile
int           renderedCatIdx  = -1;
int           renderedMode    = -2;
String        currentCategory = "bio";
String        currentMode     = "neutral";

// Firma corrente del set di decontestualizzazioni — usata per
// detectare un cambio e far partire il blink del LED solo sui
// NUOVI eventi (non a ogni rinfresco).
String        decontextSignature = "";
int           decontextCount     = 0;
// Conteggio decontestualizzazioni per categoria — popolato dal POST
// /sentence (campo "decontextCounts" mandato dall'estensione, stesso
// valore mostrato dalla dashboard). Una categoria non decontestualizzata
// resta a 0. Usato dalla info line per scrivere "N EVENT(S) ..." solo
// in corrispondenza della categoria selezionata.
int           catDecontextCount[N_CAT] = { 0, 0, 0, 0, 0, 0 };

// Stato animazione LED — 2 lampi arancioni: ON-OFF-ON-OFF (4 step da 250 ms)
enum LedAnim { LED_NONE, LED_BLINK_ORANGE };
LedAnim       ledAnim       = LED_NONE;
int           ledAnimStep   = 0;
unsigned long ledAnimNextMs = 0;
const unsigned long LED_BLINK_HALF_MS = 250;

// Ultimo colore LED applicato a regime
RGB           lastLedColor = { 1, 1, 1 };

// ── Telnet log server (Serial-over-WiFi) ──────────────────────
// L'Arduino IDE non supporta il Serial Monitor sulla porta network;
// esponiamo un server TCP su porta 23 e usiamo tlog() per scrivere
// in parallelo su Serial USB e sul client telnet connesso.
// Da Terminale: `telnet <ip>` oppure `nc <ip> 23`.
WiFiServer telnetSrv(23);
WiFiClient telnetClient;
void tlog(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
  if (telnetClient && telnetClient.connected()) telnetClient.print(buf);
}

// ── UI state machine ─────────────────────────────────────────
//   UI_IDLE          → encoder 1 rota tra categorie, command line vuota
//   UI_ACTION_SELECT → encoder 2 è in POISON/AMPLIFY: encoder 1 rota tra
//                      LOW/MID/HIGH; command line "POISON LOW MID HIGH"
//                      con la voce selezionata sottolineata.
//   UI_UNDO          → dopo un click in ACTION_SELECT: command line
//                      "UNDO Ns" che conta alla rovescia per 5 s.
//                      Un altro click annulla l'azione, altrimenti la
//                      command line torna ad ACTION_SELECT / IDLE.
//   UI_FORCE_NEUTRAL → l'utente ha commesso un poison/amplify (UNDO
//                      scaduto): schermata di blocco a tutto display
//                      finché l'encoder 2 non torna in posizione
//                      neutrale (mode == 0). Tutti gli input di
//                      encoder 1 vengono ignorati in questo stato.
enum UiState { UI_IDLE, UI_ACTION_SELECT, UI_UNDO, UI_FORCE_NEUTRAL };
volatile UiState uiState   = UI_IDLE;
volatile int     intensityIdx = 0;   // default = LOW

// True finché l'estensione non ha ancora mandato il primo POST /sentence.
// In questa fase il display mostra SOLO "AWAITING CONNECTION..." centrato,
// senza linea separatrice, senza categoria selezionata, senza command line.
// Tutti i path di redraw ridirigono a drawAwaitingScreen() finché il flag
// resta true. Diventa false in handleSentencePost al primo POST.
bool awaitingFirstSentence = true;

// Ultima azione applicata (per /action → undo lato estensione)
String        lastActionType     = "";   // "poison" | "amplify"
String        lastActionCategory = "";
int           lastActionIntensity = 0;

// Coda /action: cosa segnalare nel prossimo poll
String        pendingAction      = "";   // "" = nessuna azione pendente
String        pendingCategory    = "";
int           pendingIntensity   = 0;

// Deferred-commit UI_UNDO: il click di commit NON manda subito l'azione
// all'estensione. Memorizziamo i dettagli in deferred* e mostriamo per
// HOLD_TO_CANCEL_MS una finestra in cui l'utente può tenere premuto il
// bottone per cancellare. Se scade senza cancel, l'azione viene davvero
// inviata (pendingAction). Se l'utente la annulla tramite hold, il
// deferred viene scartato — l'estensione non vede mai l'azione e la
// dashboard non la registra. Niente più /action="undo".
String        deferredAction      = "";
String        deferredCategory    = "";
int           deferredIntensity   = 0;
unsigned long commitDeadlineMs    = 0;   // 0 = nessun commit deferred in corso
unsigned long actionMsgUntilMs    = 0;   // 0 = nessun messaggio in corso
String        actionMsgText       = "";  // "ACTION CANCELED" o "ACTION SENT"
const unsigned long HOLD_TO_CANCEL_MS = 5000;   // durata finestra cancel
const unsigned long ACTION_MSG_MS     = 1500;
const int           CANCEL_BAR_SEGMENTS = 5;    // un segmento per secondo

// Stato del command line attualmente disegnato — per evitare
// partial-refresh inutili.
String renderedCmdSignature = "";

// Decontext blink: ogni BLINK_INTERVAL alterniamo highlight nero ↔
// normale sulle parole decontestualizzate, finché decontextCount > 0
// (cioè finché l'utente non applica un'azione o l'estensione non
// cancella il flag).
bool          dcBlinkState  = false;
unsigned long dcBlinkNextMs = 0;
const unsigned long DC_BLINK_INTERVAL_MS = 800;

// "Enter action mode" hint quando l'utente clicca encoder 1 in
// UI_IDLE (encoder 2 = neutral, nessuna azione da committare).
unsigned long hintExpiresMs = 0;
const unsigned long HINT_WINDOW_MS = 2200;

// Lista parole decontestualizzate (uppercase) ricevute dall'ultimo
// POST /sentence. È quella che già usavamo per il blink del LED.
String        decontextWords[8];

// Animazione "scramble" — viene innescata quando la previsione di una
// o più categorie cambia (nuovo /sentence POST con vocaboli diversi):
// quelle parole appaiono prima come lettere random per alcuni frame,
// poi si stabilizzano sul nuovo vocabolo. Niente scramble al cambio di
// selezione utente: la rotazione dell'encoder 1 fa un partial refresh
// "secco" senza animazione.
bool          scrambleActive       = false;
uint32_t      scrambleSeed         = 0;
const int     SCRAMBLE_FRAMES      = 4;       // frame random prima della parola vera
bool          scrambleCat[N_CAT]   = { false, false, false, false, false, false };

// ─────────────────────────────────────────────────────────────
// Encoder ISRs
// ─────────────────────────────────────────────────────────────
// Encoder 1 — stesso decoder quadratura di enc2 (ENC2_QUAD_TABLE è
// riutilizzata: è solo una lookup table 16-entry). Emette delta firmati
// in enc1PendingDelta; il dispatch (categoria vs intensità vs noop in UNDO)
// è fatto nel loop dove leggere uiState non è racy.
void IRAM_ATTR enc1ISR() {
  uint8_t curr = (digitalRead(ENC1_CLK) << 1) | digitalRead(ENC1_DT);
  uint8_t idx  = ((enc1PrevState & 0x3) << 2) | curr;
  enc1PrevState = curr;
  int8_t delta = -ENC2_QUAD_TABLE[idx];   // negato per allineare CW = avanti
  if (delta == 0) return;                 // transizione invalida → ignora
  enc1SubStep += delta;
  if (enc1SubStep >= ENC2_TRANSITIONS_PER_DETENT) {
    enc1SubStep -= ENC2_TRANSITIONS_PER_DETENT;
    enc1PendingDelta++;
  } else if (enc1SubStep <= -ENC2_TRANSITIONS_PER_DETENT) {
    enc1SubStep += ENC2_TRANSITIONS_PER_DETENT;
    enc1PendingDelta--;
  }
}

void IRAM_ATTR enc1SwISR() {
  static unsigned long last = 0;
  unsigned long t = millis();
  if (t - last < 200) return;
  last = t;
  actionFresh = true;
}

// ISR a quadratura: viene chiamata su OGNI cambio di CLK e di DT.
// Niente debounce a tempo — il rimbalzo viene filtrato dalla tabella
// delle transizioni (ENC2_QUAD_TABLE), che restituisce 0 sui pattern
// non validi. Risultato: 1 detent fisico = 1 incremento netto di enc2EncPos.
void IRAM_ATTR enc2ISR() {
  uint8_t curr = (digitalRead(ENC2_CLK) << 1) | digitalRead(ENC2_DT);
  uint8_t idx  = ((enc2PrevState & 0x3) << 2) | curr;
  enc2PrevState = curr;
  // Negazione del delta: il cablaggio fisico CLK/DT è invertito
  // rispetto alla convenzione della tabella, quindi CW deve crescere.
  int8_t delta = -ENC2_QUAD_TABLE[idx];
  if (delta == 0) return;            // transizione non valida → ignora
  enc2SubStep += delta;
  if (enc2SubStep >= ENC2_TRANSITIONS_PER_DETENT) {
    enc2SubStep -= ENC2_TRANSITIONS_PER_DETENT;
    if (enc2EncPos < ENC2_POS_MAX) enc2EncPos++;
  } else if (enc2SubStep <= -ENC2_TRANSITIONS_PER_DETENT) {
    enc2SubStep += ENC2_TRANSITIONS_PER_DETENT;
    if (enc2EncPos > 0)            enc2EncPos--;
  }
}

// Mappa posizione assoluta 0..10 → modo logico (neutral al centro):
//   pos  0..3 → amplify (+1)   ← fondo corsa CCW
//   pos  4..6 → neutral ( 0)   ← centro, boot default
//   pos 7..10 → poison  (-1)   ← fondo corsa CW
int modeFromPos(int pos) {
  if (pos < ENC2_NEUTRAL_START) return  1;   // amplify
  if (pos < ENC2_POISON_START)  return  0;   // neutral
  return                              -1;    // poison
}

// ─────────────────────────────────────────────────────────────
// Helpers stato
// ─────────────────────────────────────────────────────────────
const char* modeStr(int v) {
  if (v < 0) return "poison";
  if (v > 0) return "amplify";
  return "neutral";
}

int categoryIdxOf(const String& key) {
  for (int i = 0; i < N_CAT; i++) {
    if (key == CAT_KEYS[i]) return i;
  }
  return -1;
}

// Confronto parola sentence ↔ vocabolo categoria — toglie la
// punteggiatura finale (es. "DEVELOPER," → "DEVELOPER").
// Toglie punteggiatura e parentesi (anteriori e posteriori) dalla parola
// nella frase, poi confronta carattere per carattere con il vocabolo.
// Indispensabile ora che la frase arriva nella forma "...[YOUNG], ...".
bool wordMatchesVocable(const String& sentWord, const String& vocable) {
  if (vocable.length() == 0) return false;
  int start = 0, end = sentWord.length();
  while (start < end && !isAlphaNumeric(sentWord[start])) start++;
  while (end > start && !isAlphaNumeric(sentWord[end - 1])) end--;
  if (end - start != (int)vocable.length()) return false;
  for (int i = 0; i < end - start; i++) {
    if (sentWord[start + i] != vocable[i]) return false;
  }
  return true;
}

// ─────────────────────────────────────────────────────────────
// LED RGB
// ─────────────────────────────────────────────────────────────
inline void writeChannel(int pin, uint8_t v) {
  analogWrite(pin, LED_COMMON_ANODE ? (255 - v) : v);
}

void writeLED(RGB c) {
  writeChannel(LED_R, c.r);
  writeChannel(LED_G, c.g);
  writeChannel(LED_B, c.b);
  // Tieni traccia di ogni write diretta così applySteadyLED non finisce a
  // "no-op" credendo che lo stato a regime sia ancora valido dopo una
  // scrittura manuale (es. blink di decontestualizzazione).
  lastLedColor = c;
}

// Colore LED a regime: solo modo (no decontext qui — il decontext
// è solo un blink temporaneo, non uno stato persistente).
RGB steadyColor(int mode) {
  if (mode > 0) return COL_GREEN;
  if (mode < 0) return COL_PURPLE;
  return COL_OFF;
}

void applySteadyLED(int mode) {
  RGB c = steadyColor(mode);
  if (c.r != lastLedColor.r || c.g != lastLedColor.g || c.b != lastLedColor.b) {
    writeLED(c);
    lastLedColor = c;
  }
}

void triggerDecontextBlink() {
  ledAnim       = LED_BLINK_ORANGE;
  ledAnimStep   = 0;
  ledAnimNextMs = millis();
}

// Avanza il blink (2 lampi). Step 0,2 = ON arancione; 1,3 = OFF.
void tickLedAnim(int mode) {
  if (ledAnim == LED_NONE) return;
  unsigned long now = millis();
  if (now < ledAnimNextMs) return;
  if (ledAnimStep >= 4) {
    ledAnim = LED_NONE;
    applySteadyLED(mode);
    return;
  }
  RGB c = (ledAnimStep % 2 == 0) ? COL_ORANGE : COL_OFF;
  writeLED(c);
  lastLedColor = c;
  ledAnimStep++;
  ledAnimNextMs = now + LED_BLINK_HALF_MS;
}

// ─────────────────────────────────────────────────────────────
// Rendering display
// ─────────────────────────────────────────────────────────────

// Helper — cerca il range [start..end] di parole consecutive della
// frase che combacia con un vocabolo multi-parola. Restituisce -1 se
// non trovato.
int findVocableRange(String words[], int wCount, const String& voc, int& outEnd) {
  outEnd = -1;
  if (voc.length() == 0) return -1;
  String vWords[8]; int vWCount = 0;
  {
    int i = 0;
    while (i <= (int)voc.length() && vWCount < 8) {
      int sp = voc.indexOf(' ', i);
      if (sp == -1) sp = voc.length();
      String w = voc.substring(i, sp);
      if (w.length() > 0) vWords[vWCount++] = w;
      i = sp + 1;
    }
  }
  if (vWCount == 0) return -1;
  for (int i = 0; i <= wCount - vWCount; i++) {
    bool match = true;
    for (int j = 0; j < vWCount; j++) {
      if (!wordMatchesVocable(words[i + j], vWords[j])) { match = false; break; }
    }
    if (match) {
      outEnd = i + vWCount - 1;
      return i;
    }
  }
  return -1;
}

// Disegna la frase con IBM Plex Mono 6pt (~7x8 cell, cap-height 7).
// Tre stati visivi per parola:
//  - SELEZIONATA (encoder 1) → sfondo nero stabile, testo bianco
//  - DECONTESTUALIZZATA      → lampeggio del testo (visibile ↔ invisibile,
//                              senza sfondo nero)
//  - resto                   → testo nero su bianco
void drawSentence(int catIdx) {
  display.fillScreen(GxEPD_WHITE);
  // Linea di separazione frase/command line, ridisegnata su OGNI
  // refresh dell'area frase per evitare ghosting/fade: i partial
  // window sopra includono la riga y=SENT_H proprio per questo.
  display.drawFastHLine(0, SENT_H, SCREEN_W, GxEPD_BLACK);
  display.setFont(&IBMPlexMono_Regular6pt7b);
  display.setTextWrap(false);
  display.setTextColor(GxEPD_BLACK);

  // Metriche di IBM Plex Mono 6pt (mono, baseline cursor)
  const int CHAR_W = 7;      // xAdvance uniforme
  const int LINE_H = 12;     // un po' meno di yAdvance (15)
  const int ASCENT = 7;      // altezza cassa maiuscola sopra baseline (yOffset -7)
  const int MARGIN = 6;
  const int MAX_BASELINE = SENT_H - 2;
  const int spaceW = CHAR_W;

  String s = currentSentence;
  s.toUpperCase();

  // Tokenizza la frase
  String words[80];
  int wCount = 0;
  {
    int i = 0;
    while (i <= (int)s.length() && wCount < 80) {
      int sp = s.indexOf(' ', i);
      if (sp == -1) sp = s.length();
      String w = s.substring(i, sp);
      if (w.length() > 0) words[wCount++] = w;
      i = sp + 1;
    }
  }

  // Range della parola SELEZIONATA (può essere multi-parola).
  // Se la categoria selezionata non ha dato (vocabolo vuoto), facciamo
  // fallback sul token "[?]" corrispondente: contiamo quanti "[?]"
  // precedono nella frase (in base alla posizione fissa della categoria
  // nel template) e evidenziamo l'N-esimo. La frase arriva
  // dall'estensione già nella forma "...[YOUNG], ... [?], ...".
  String selectedVoc = (catIdx >= 0 && catIdx < N_CAT) ? catVocables[catIdx] : "";
  int hlEnd = -1;
  int hlStart = -1;
  if (selectedVoc.length() > 0) {
    hlStart = findVocableRange(words, wCount, selectedVoc, hlEnd);
  } else if (catIdx >= 0 && catIdx < N_CAT) {
    int selPos = CAT_SENTENCE_POS[catIdx];
    int skip = 0;
    for (int j = 0; j < N_CAT; j++) {
      if (j != catIdx && catVocables[j].length() == 0
          && CAT_SENTENCE_POS[j] < selPos) skip++;
    }
    int seen = 0;
    for (int i = 0; i < wCount; i++) {
      const String& w = words[i];
      if (w.length() >= 3 && w[0] == '[' && w[1] == '?' && w[2] == ']') {
        if (seen == skip) { hlStart = i; hlEnd = i; break; }
        seen++;
      }
    }
  }

  // Range di TUTTE le parole decontestualizzate
  int dcStart[8], dcEndArr[8]; int dcRanges = 0;
  for (int k = 0; k < decontextCount && k < 8 && dcRanges < 8; k++) {
    int e;
    int st = findVocableRange(words, wCount, decontextWords[k], e);
    if (st >= 0) { dcStart[dcRanges] = st; dcEndArr[dcRanges] = e; dcRanges++; }
  }

  // Range delle categorie che devono lampeggiare di lettere random
  // (scramble): popolate al di fuori, in handleSentencePost, quando la
  // previsione di una o più categorie cambia.
  int scStart[N_CAT], scEndArr[N_CAT]; int scRanges = 0;
  if (scrambleActive) {
    for (int c = 0; c < N_CAT && scRanges < N_CAT; c++) {
      if (!scrambleCat[c]) continue;
      if (catVocables[c].length() == 0) continue;
      int e;
      int st = findVocableRange(words, wCount, catVocables[c], e);
      if (st >= 0) { scStart[scRanges] = st; scEndArr[scRanges] = e; scRanges++; }
    }
  }

  int cursorX = MARGIN;
  int cursorY = MARGIN + ASCENT;        // baseline della prima riga
  bool prevHighlighted = false;

  for (int i = 0; i < wCount; i++) {
    int wbw = words[i].length() * CHAR_W;
    int needed = (cursorX > MARGIN ? spaceW : 0) + wbw;
    if (cursorX + needed > SCREEN_W - MARGIN) {
      cursorY += LINE_H;
      cursorX = MARGIN;
      prevHighlighted = false;
      if (cursorY > MAX_BASELINE) return;
    } else if (cursorX > MARGIN) {
      cursorX += spaceW;
    }

    bool inSelection = (i >= hlStart && i <= hlEnd);
    bool inDecontext = false;
    for (int r = 0; r < dcRanges; r++) {
      if (i >= dcStart[r] && i <= dcEndArr[r]) { inDecontext = true; break; }
    }
    bool inScramble = false;
    for (int r = 0; r < scRanges; r++) {
      if (i >= scStart[r] && i <= scEndArr[r]) { inScramble = true; break; }
    }

    // Solo la selezione ha lo sfondo nero. La decontestualizzazione invece
    // fa LAMPEGGIARE il testo (visibile ↔ invisibile, nessun riquadro):
    // quando dcBlinkState è false e la parola è decontext, saltiamo il
    // print() lasciando la cella vuota — lo spazio resta perché cursorX
    // avanza comunque di wbw.
    bool blackBg  = inSelection;
    bool hideText = inDecontext && !dcBlinkState;

    if (blackBg) {
      int x = cursorX - 1;
      int w = wbw + 2;
      if (prevHighlighted && cursorX > MARGIN) {
        x -= spaceW;
        w += spaceW;
      }
      // Con font custom GFX, setTextColor(fg, bg) non riempie lo sfondo:
      // disegniamo prima il riquadro nero, poi solo i pixel del glifo.
      display.fillRect(x, cursorY - ASCENT - 1, w, LINE_H, GxEPD_BLACK);
      display.setTextColor(GxEPD_WHITE);
    } else {
      display.setTextColor(GxEPD_BLACK);
    }
    if (!hideText) {
      display.setCursor(cursorX, cursorY);
      if (scrambleActive && inScramble) {
        // Sostituisce i glifi con lettere maiuscole random — la sequenza
        // è derivata da scrambleSeed + indice parola + indice glifo, così
        // ogni rerender dello stesso frame produce sempre gli stessi
        // caratteri (no flicker se l'e-ink ripagina).
        for (int k = 0; k < (int)words[i].length(); k++) {
          uint32_t h = scrambleSeed * 2654435761u
                     + (uint32_t)i * 1000003u
                     + (uint32_t)k * 65537u;
          display.write((char)('A' + (h % 26)));
        }
      } else {
        display.print(words[i]);
      }
    }
    cursorX += wbw;
    prevHighlighted = blackBg;
  }

  if (selectedVoc.length() > 0 && hlStart < 0) {
    Serial.print("drawSentence: vocable '");
    Serial.print(selectedVoc);
    Serial.println("' not found in sentence");
  }
}

// Costruisce una "signature" del contenuto della command line; se è
// invariato non serve ridisegnare (e quindi evitiamo ghosting da
// partial-refresh inutili sull'e-ink).
String cmdLineSignature(int mode, int intIdx) {
  if (awaitingFirstSentence) return String("AWAIT");
  if (uiState == UI_FORCE_NEUTRAL) {
    return String("FN");
  }
  if (uiState == UI_UNDO) {
    // 2 sotto-stati: messaggio finale (ACTION CANCELED/SENT) oppure
    // istruzione + barra che si riempie 1 segmento al secondo per la
    // durata della finestra di commit deferred. Signature distinta per
    // ogni segmento riempito così la command line si ridisegna 5 volte
    // durante la finestra, non a ogni iter del loop.
    if (actionMsgUntilMs > millis()) return String("U:M:") + actionMsgText;
    int elapsed = (commitDeadlineMs > millis())
      ? (int)((HOLD_TO_CANCEL_MS - (commitDeadlineMs - millis())) / 1000)
      : (int)(HOLD_TO_CANCEL_MS / 1000);
    if (elapsed < 0) elapsed = 0;
    if (elapsed > CANCEL_BAR_SEGMENTS) elapsed = CANCEL_BAR_SEGMENTS;
    return String("U:B:") + String(elapsed);
  }
  if (uiState == UI_ACTION_SELECT) {
    return String(mode > 0 ? "A:" : "P:") + String(intIdx);
  }
  // UI_IDLE: hint "ENTER ACTION MODE" finché hintExpiresMs è nel futuro
  if (hintExpiresMs > millis()) return "H";
  // UI_IDLE a regime: la riga mostra la categoria selezionata e (se quella
  // categoria ha eventi decontestualizzati) il conteggio della SOLA
  // categoria selezionata. Includiamo entrambi nella signature così il
  // partial refresh scatta quando uno dei due cambia.
  int catDc = (renderedCatIdx >= 0 && renderedCatIdx < N_CAT)
              ? catDecontextCount[renderedCatIdx] : 0;
  return String("I:") + String(renderedCatIdx) + ":" + String(catDc);
}

// Disegna SOLO la riga in basso. Stesso stile della frase: IBMPlexMono 6pt
// monospace (cella 7x8, cap-height 7). Y_TEXT è la BASELINE del font
// custom (setCursor su GFX fonts pone la baseline).
void drawCommandLine(int mode, int intIdx) {
  const int Y_TOP    = SENT_H + 2;          // 104
  const int ASCENT   = 7;                   // cap-height IBMPlex 6pt
  const int Y_TEXT   = Y_TOP + 3 + ASCENT;  // 114: baseline (text spans 107..114)
  const int CHAR_W   = 7;                   // IBMPlex 6pt xAdvance
  const int MARGIN   = 6;

  display.fillRect(0, SENT_H, SCREEN_W, SCREEN_H - SENT_H, GxEPD_WHITE);
  display.drawFastHLine(0, SENT_H, SCREEN_W, GxEPD_BLACK);
  display.setFont(&IBMPlexMono_Regular6pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setTextWrap(false);

  if (uiState == UI_UNDO) {
    // Layout finestra commit deferred:
    //   [HOLD 5 SEC TO CANCEL]  [█][█][░][░][░]
    // Testo a sinistra + barra a destra sulla stessa riga. La barra è
    // un countdown sul commit deferred: si riempie 1 segmento al secondo
    // per HOLD_TO_CANCEL_MS; alla fine o l'azione viene mandata (ACTION
    // SENT) oppure annullata (ACTION CANCELED) in base a se l'utente
    // sta tenendo il bottone premuto.
    if (actionMsgUntilMs > millis()) {
      display.setCursor(MARGIN, Y_TEXT);
      display.print(actionMsgText);
      return;
    }
    const char* prompt = "CLICK TO REVOKE";
    display.setCursor(MARGIN, Y_TEXT);
    display.print(prompt);

    // Testo più corto (15 char vs 21 di prima) → segmenti più larghi:
    // bar prominente e leggibile da lontano.
    const int BAR_SEG_W   = 22;
    const int BAR_SEG_GAP = 2;
    const int BAR_SEG_H   = 10;
    const int BAR_Y       = Y_TEXT - ASCENT;
    int barX = MARGIN + (int)strlen(prompt) * CHAR_W + CHAR_W;  // gap di 1 char
    int elapsed = (commitDeadlineMs > millis())
      ? (int)((HOLD_TO_CANCEL_MS - (commitDeadlineMs - millis())) / 1000)
      : (int)(HOLD_TO_CANCEL_MS / 1000);
    if (elapsed < 0) elapsed = 0;
    if (elapsed > CANCEL_BAR_SEGMENTS) elapsed = CANCEL_BAR_SEGMENTS;
    for (int i = 0; i < CANCEL_BAR_SEGMENTS; i++) {
      int sx = barX + i * (BAR_SEG_W + BAR_SEG_GAP);
      if (i < elapsed) {
        display.fillRect(sx, BAR_Y, BAR_SEG_W, BAR_SEG_H, GxEPD_BLACK);
      } else {
        display.drawRect(sx, BAR_Y, BAR_SEG_W, BAR_SEG_H, GxEPD_BLACK);
      }
    }
    return;
  }

  if (uiState == UI_IDLE && hintExpiresMs > millis()) {
    display.setCursor(MARGIN, Y_TEXT);
    display.print("ENTER ACTION MODE");
    return;
  }

  if (uiState == UI_ACTION_SELECT) {
    const char* head = (mode > 0) ? "AMPLIFY" : "POISON";
    display.setCursor(MARGIN, Y_TEXT);
    display.print(head);

    // Barra di intensità a 3 segmenti: i primi (intIdx+1) sono pieni, gli
    // altri solo contornati. La label corrente (LOW/MID/HIGH) sta a destra
    // della barra. Larghezza totale entro 250 px anche col font IBMPlex
    // (CHAR_W=7) e head="AMPLIFY" (7 char) + label="HIGH" (4 char).
    const int BAR_SEG_W = 18;
    const int BAR_SEG_H = 8;
    const int BAR_SEG_GAP = 2;
    const int BAR_Y = Y_TEXT - ASCENT;      // 107: allineato col TOP del testo
    const int GAP_AFTER_HEAD = 2 * CHAR_W;  // 2 char spazio tra head e barra
    const int GAP_BEFORE_LABEL = CHAR_W;    // 1 char spazio tra barra e label

    int barX = MARGIN + (int)strlen(head) * CHAR_W + GAP_AFTER_HEAD;
    int fillCount = intIdx + 1;             // 1..3 segmenti pieni
    for (int i = 0; i < N_INT; i++) {
      int sx = barX + i * (BAR_SEG_W + BAR_SEG_GAP);
      if (i < fillCount) {
        display.fillRect(sx, BAR_Y, BAR_SEG_W, BAR_SEG_H, GxEPD_BLACK);
      } else {
        display.drawRect(sx, BAR_Y, BAR_SEG_W, BAR_SEG_H, GxEPD_BLACK);
      }
    }

    int barEndX = barX + N_INT * (BAR_SEG_W + BAR_SEG_GAP) - BAR_SEG_GAP;
    int labelX  = barEndX + GAP_BEFORE_LABEL;
    display.setCursor(labelX, Y_TEXT);
    display.print(INT_LABELS[intIdx]);
    return;
  }
  // UI_IDLE (neutral): a sinistra il nome della categoria selezionata.
  // Il conteggio "N EVENT(S) DECONTEXTUALISED" appare a destra SOLO se la
  // categoria attualmente selezionata ha eventi decontestualizzati — la
  // sorgente del conteggio è la dashboard (campo decontextCounts del POST
  // /sentence). Se la selezione è scaduta (renderedCatIdx == -1) la riga
  // resta bianca.
  if (renderedCatIdx >= 0 && renderedCatIdx < N_CAT) {
    const char* catName = CAT_FULL_NAMES[renderedCatIdx];
    display.setCursor(MARGIN, Y_TEXT);
    display.print(catName);
    int catDc = catDecontextCount[renderedCatIdx];
    if (catDc > 0) {
      // "N DECONTEXT." sta accanto a qualsiasi nome categoria, incluso
      // PSYCHO-BEHAVIOURAL (18 char × 7 = 126 px + 8 char × 7 = 56 px
      // + margini = 194 px). Niente più fallback al solo numero.
      String info = String(catDc) + " DECONTEXT.";
      int leftEnd = MARGIN + (int)strlen(catName) * CHAR_W;
      int infoW = (int)info.length() * CHAR_W;
      int infoX = SCREEN_W - MARGIN - infoW;
      if (infoX < leftEnd + CHAR_W) infoX = leftEnd + CHAR_W;
      display.setCursor(infoX, Y_TEXT);
      display.print(info);
    }
  }
}

// ── Schermata "AWAITING CONNECTION..." ──────────────────────
// Disegnata finché awaitingFirstSentence == true (boot completato ma
// nessun POST /sentence ricevuto). Layout top-left che combina REPLICA_,
// stato WiFi (CONNECTED + IP) e il messaggio "AWAITING CONNECTION..."
// in un'unica vista. Stesso font della frase principale (IBMPlexMono
// 6pt) — setCursor pone la baseline, non il top-left.
void drawAwaitingScreen() {
  display.fillScreen(GxEPD_WHITE);
  display.setFont(&IBMPlexMono_Regular6pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setTextWrap(false);

  const int X = 6;
  const int ASCENT = 7;

  // Baseline = top + ASCENT. Mantengo le stesse Y "top" di drawBootScreen
  // (6, 36, 50) e aggiungo l'awaiting più sotto.
  display.setCursor(X, 6 + ASCENT);          // 13
  display.print("REPLICA_");

  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(X, 36 + ASCENT);       // 43
    display.print("CONNECTED");
    display.setCursor(X, 50 + ASCENT);       // 57
    display.print(WiFi.localIP().toString());
  } else {
    display.setCursor(X, 36 + ASCENT);
    display.print("WIFI DOWN");
  }

  display.setCursor(X, 80 + ASCENT);         // 87
  display.print("AWAITING DASHBOARD CONNECTION...");
}

void redrawAwaitingFull() {
  display.setRotation(3);
  display.setFullWindow();
  display.firstPage();
  do { drawAwaitingScreen(); } while (display.nextPage());
  renderedCmdSignature = "AWAIT";
}

// Full refresh — chiamato al cambio frase (resetta ghosting)
void redrawAllFull(int catIdx, int mode) {
  if (awaitingFirstSentence) { redrawAwaitingFull(); return; }
  display.setRotation(3);
  display.setFullWindow();
  display.firstPage();
  do {
    drawSentence(catIdx);
    drawCommandLine(mode, intensityIdx);
  } while (display.nextPage());
  renderedCmdSignature = cmdLineSignature(mode, intensityIdx);
}

// Partial refresh dell'area frase + command line — usato quando
// l'encoder 1 ruota la categoria (in UI_IDLE).
void redrawAllPartial(int catIdx, int mode) {
  if (awaitingFirstSentence) return;   // schermo awaiting: nessuna UI da rinfrescare
  display.setRotation(3);
  display.setPartialWindow(0, 0, SCREEN_W, SCREEN_H);
  display.firstPage();
  do {
    drawSentence(catIdx);
    drawCommandLine(mode, intensityIdx);
  } while (display.nextPage());
  renderedCmdSignature = cmdLineSignature(mode, intensityIdx);
}

// Esegue l'animazione di scramble sulla frase: SCRAMBLE_FRAMES frame
// di lettere random sulle categorie segnate in scrambleCat[], poi reset.
// Il "frame finale" col testo reale lo dipinge il chiamante (di solito
// redrawAllFull dopo l'arrivo di un nuovo /sentence).
void runScrambleAnimation(int catIdxForHighlight) {
  for (int f = 0; f < SCRAMBLE_FRAMES; f++) {
    scrambleSeed   = (uint32_t)(millis() ^ (f * 0x9E3779B9u));
    scrambleActive = true;
    display.setRotation(3);
    // +1 → include la riga separatrice y=SENT_H nel refresh.
    display.setPartialWindow(0, 0, SCREEN_W, SENT_H + 1);
    display.firstPage();
    do { drawSentence(catIdxForHighlight); } while (display.nextPage());
  }
  scrambleActive = false;
  for (int i = 0; i < N_CAT; i++) scrambleCat[i] = false;
}

// Partial refresh della sola command line — usato durante
// UI_ACTION_SELECT (cambio intensità) e UI_UNDO (countdown).
void redrawCommandLineOnly(int mode) {
  if (awaitingFirstSentence) return;   // niente command line in awaiting
  display.setRotation(3);
  display.setPartialWindow(0, SENT_H, SCREEN_W, SCREEN_H - SENT_H);
  display.firstPage();
  do { drawCommandLine(mode, intensityIdx); } while (display.nextPage());
  renderedCmdSignature = cmdLineSignature(mode, intensityIdx);
}

// ── Schermata di blocco "RETURN TO NEUTRAL" ──────────────────
// Disegnata a tutto display quando l'utente ha appena commesso una
// azione (poison/amplify) e deve riportare l'encoder 2 a 0 prima di
// poter interagire di nuovo. Usa il bitmap built-in 5x7 con setTextSize
// per avere un titolo grande senza dipendere da font extra.
void drawForceNeutralOverlay() {
  display.fillRect(0, 0, SCREEN_W, SCREEN_H, GxEPD_WHITE);
  // Stesso font della frase (IBM Plex Mono 6pt, monospace, cap-height 7 px,
  // cella 7x8): tutte le righe della modale usano questa taglia.
  display.setFont(&IBMPlexMono_Regular6pt7b);
  display.setTextSize(1);
  display.setTextColor(GxEPD_BLACK);
  display.setTextWrap(false);

  const int LINE_H = 12;
  const int ASCENT = 7;

  const char* lines[] = { "ACTION SET.", "RETURN TO NEUTRAL MODE" };
  const int N_LINES = 2;

  // Layout top-left, X e Y baseline coerenti con drawSentence (MARGIN=6,
  // baseline prima riga = MARGIN + ASCENT).
  const int X = 6;
  const int Y0 = 6 + ASCENT;       // 13: baseline della prima riga
  for (int i = 0; i < N_LINES; i++) {
    display.setCursor(X, Y0 + i * LINE_H);
    display.print(lines[i]);
  }
}

void redrawForceNeutralFull() {
  display.setRotation(3);
  display.setFullWindow();
  display.firstPage();
  do { drawForceNeutralOverlay(); } while (display.nextPage());
  renderedCmdSignature = cmdLineSignature(-99, 0);   // forza la sig a "FN"
}

// ─────────────────────────────────────────────────────────────
// HTTP handlers
// ─────────────────────────────────────────────────────────────

void cors() {
  server.sendHeader("Access-Control-Allow-Origin",  "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  // Chrome Private Network Access: senza questo header un fetch dal
  // service worker dell'estensione verso un IP privato fallisce con
  // "Failed to fetch" prima ancora di raggiungere il dispositivo.
  server.sendHeader("Access-Control-Allow-Private-Network", "true");
}

void handleOptions() {
  cors();
  server.send(204);
}

void handleStatus() {
  cors();
  StaticJsonDocument<192> doc;
  doc["connected"] = true;
  doc["action"]    = currentMode;
  doc["category"]  = currentCategory;
  doc["decontext"] = decontextCount;
  doc["ip"]        = WiFi.localIP().toString();
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// POST /sentence
// body: { "text": "...", "decontext": ["WORD1", ...], "vocables": { "bio": "...", ... } }
void handleSentencePost() {
  cors();
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"empty body\"}");
    return;
  }
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }
  const char* txt = doc["text"];
  if (txt) currentSentence = String(txt);

  // Mappa categoria → vocabolo (e diff con la previsione precedente:
  // ogni categoria con vocabolo nuovo non vuoto viene marcata per
  // l'animazione di scramble).
  JsonObject vocObj = doc["vocables"].as<JsonObject>();
  Serial.print("vocables ->");
  bool anyScramble = false;
  for (int i = 0; i < N_CAT; i++) {
    String oldVoc = catVocables[i];
    catVocables[i] = "";
    if (!vocObj.isNull()) {
      const char* v = vocObj[CAT_KEYS[i]];
      if (v) {
        String s = String(v);
        s.toUpperCase();
        catVocables[i] = s;
      }
    }
    scrambleCat[i] = (catVocables[i].length() > 0 && catVocables[i] != oldVoc);
    if (scrambleCat[i]) anyScramble = true;
    Serial.print(' '); Serial.print(CAT_KEYS[i]); Serial.print('=');
    Serial.print(catVocables[i].length() ? catVocables[i] : String("?"));
    if (scrambleCat[i]) Serial.print('*');   // segna i cambiati nel log
  }
  Serial.println();

  // Set decontestualizzato + signature per detect del cambio
  String newSig = "";
  decontextCount = 0;
  for (int i = 0; i < 8; i++) decontextWords[i] = "";
  JsonVariant dcVar = doc["decontext"];
  if (dcVar.is<JsonArray>()) {
    JsonArray dcArr = dcVar.as<JsonArray>();
    for (JsonVariant v : dcArr) {
      String w = v.as<String>();
      w.toUpperCase();
      if (w.length() == 0) continue;
      newSig += w + "|";
      if (decontextCount < 8) decontextWords[decontextCount] = w;
      decontextCount++;
    }
  }
  // Mappa per-categoria col conteggio mandato dalla dashboard. Categorie
  // mancanti dal JSON si azzerano (la dashboard non manda le "pulite").
  // Usiamo l'operatore | per leggere l'int con default 0 — più robusto di
  // is<int>(), che in ArduinoJson v6 ha edge case su numeri JSON piccoli.
  JsonVariantConst dcCountVar = doc["decontextCounts"];
  Serial.print("dcCounts ->");
  for (int i = 0; i < N_CAT; i++) {
    int n = dcCountVar[CAT_KEYS[i]] | 0;
    if (n < 0) n = 0;
    catDecontextCount[i] = n;
    Serial.print(' '); Serial.print(CAT_KEYS[i]); Serial.print('=');
    Serial.print(n);
  }
  Serial.println();
  // Aggiorniamo la signature per coerenza con altre parti del firmware,
  // ma NON la usiamo più per gatekeepare il blink: lampeggiamo ogni
  // volta che arriva un POST con almeno una parola decontestualizzata,
  // anche se la categoria era già decontestualizzata (l'evento è comunque
  // un nuovo "decontext event" loggato dall'estensione).
  decontextSignature = newSig;
  bool hasDecontext = (decontextCount > 0);

  server.send(200, "application/json", "{\"ok\":true}");
  // Blink arancione SINCRONO prima del redraw — SOLO se siamo in neutral.
  // In poison/amplify il LED dell'azione (viola/verde) deve restare ACCESO
  // FISSO per tutta la durata del modo azione, finché l'utente non torna
  // in neutral. Niente lampeggi di altri eventi che interrompono il
  // segnale "sei attualmente in modo azione".
  if (hasDecontext && renderedMode == 0) {
    for (int i = 0; i < 2; i++) {
      writeLED(COL_ORANGE);
      delay(250);
      writeLED(COL_OFF);
      delay(250);
    }
    applySteadyLED(renderedMode);
  }
  // Scramble pre-refresh sulle previsioni cambiate (se ce ne sono).
  // Il flag scrambleCat[] è già stato popolato dal diff sopra.
  // In UI_FORCE_NEUTRAL i dati arrivati restano in memoria ma NON
  // ridisegniamo: la schermata di blocco deve restare visibile finché
  // l'utente non riporta l'encoder 2 a neutrale.
  if (uiState == UI_FORCE_NEUTRAL) return;
  // Prima frase ricevuta → esci dallo schermo "AWAITING CONNECTION...".
  // Lo facciamo PRIMA del redraw così redrawAllFull non finisce a
  // ridisegnare lo schermo di awaiting.
  awaitingFirstSentence = false;
  int catForHighlight = renderedCatIdx >= 0 ? renderedCatIdx : 0;
  if (anyScramble) {
    runScrambleAnimation(catForHighlight);
  }
  redrawAllFull(catForHighlight, renderedMode);
}

// /action — risponde con:
//   - action: "poison" | "amplify" | "undo" se c'è qualcosa di
//             pendente da applicare; altrimenti il modo corrente
//             (utile per side-effect tipo "modo neutral").
//   - category, intensity: validi quando fresh=true.
//   - fresh: true una volta sola dopo un click dell'encoder 1.
void handleActionGet() {
  cors();
  StaticJsonDocument<160> doc;
  bool fresh = pendingAction.length() > 0;
  doc["action"]    = fresh ? pendingAction   : currentMode;
  doc["category"]  = fresh ? pendingCategory : currentCategory;
  doc["intensity"] = fresh ? pendingIntensity : 0;
  doc["fresh"]     = fresh;
  // Consuma la pending: l'estensione applica adesso.
  if (fresh) {
    pendingAction    = "";
    pendingCategory  = "";
    pendingIntensity = 0;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// ─────────────────────────────────────────────────────────────
// Boot screen
// ─────────────────────────────────────────────────────────────
void drawBootScreen(const char* line1, const char* line2) {
  // IBMPlexMono 6pt non ha glifi minuscoli: senza toUpperCase i call
  // site con "Connecting...", "retrying...", "in progress..." renderebbero
  // come small-caps/glifi mancanti. Forziamo upper qui per uniformità.
  String l1 = String(line1); l1.toUpperCase();
  String l2 = String(line2); l2.toUpperCase();

  display.setRotation(3);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&IBMPlexMono_Regular6pt7b);   // stesso font della frase
    display.setTextWrap(false);
    const int ASCENT = 7;                          // cap-height IBMPlex 6pt
    display.setCursor(6, 6 + ASCENT);              // 13: baseline REPLICA_
    display.print("REPLICA_");
    display.setCursor(6, 36 + ASCENT);             // 43: baseline line1
    display.print(l1);
    display.setCursor(6, 50 + ASCENT);             // 57: baseline line2
    display.print(l2);
  } while (display.nextPage());
}

// ─────────────────────────────────────────────────────────────
// WiFi
// ─────────────────────────────────────────────────────────────
// Stampa tutte le reti 2.4 GHz visibili e segnala se il SSID
// configurato è tra queste. Diagnostica chiave per status=6:
// 99% delle volte significa "SSID non visibile" → 5 GHz, hotspot
// spento, distanza eccessiva, o nome con uno spazio/case sbagliato.
void wifiScanReport() {
  // Reset esplicito della radio: dopo un begin() fallito lo stato STA
  // è spesso "appeso" e scanNetworks() ritorna -2 (SCAN_FAILED).
  Serial.println("[WiFi] resetting radio before scan...");
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_STA);
  delay(200);

  Serial.println("[WiFi] scanning visible 2.4 GHz networks...");
  int n = WiFi.scanNetworks(/*async*/false, /*show_hidden*/true);
  // n: >=0 numero di AP, -1 SCAN_RUNNING, -2 SCAN_FAILED.
  if (n < 0) {
    Serial.printf("[WiFi] scan: ERROR rc=%d (-2=SCAN_FAILED, -1=RUNNING)\n", n);
    Serial.println("[WiFi]   → la radio del Nano ESP32 NON risponde. Cause comuni:");
    Serial.println("[WiFi]     1) alimentazione USB insufficiente: prova un altro cavo o");
    Serial.println("[WiFi]        un hub alimentato. La WiFi tira picchi >400 mA.");
    Serial.println("[WiFi]     2) un componente esterno (display e-ink, encoder) sta");
    Serial.println("[WiFi]        succhiando troppa corrente nello stesso momento.");
    Serial.println("[WiFi]     3) richiede un power-cycle completo: stacca/riattacca USB.");
    Serial.println("[WiFi]     4) in rari casi: aggiorna i bootloader del Nano ESP32 via IDE.");
    return;
  }
  if (n == 0) {
    Serial.println("[WiFi] scan: 0 reti visibili — la radio funziona ma non sente nulla.");
    Serial.println("[WiFi]   → sei in un punto cieco? Avvicina il device all'AP/hotspot.");
    return;
  }
  bool seen = false;
  int  seenIdx = -1;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    int rssi    = WiFi.RSSI(i);
    int ch      = WiFi.channel(i);
    Serial.printf("  %2d) ch=%2d  rssi=%4d dBm  ssid='%s'\n",
                  i + 1, ch, rssi, ssid.c_str());
    if (ssid == WIFI_SSID) { seen = true; seenIdx = i; }
  }
  if (seen) {
    // Stampa auth mode / canale / RSSI dell'AP che vogliamo: con questo
    // si capisce se è solo WPA3 (negoziazione PMF), WPA2 (password
    // probabilmente sbagliata) o segnale troppo debole.
    int rssi = WiFi.RSSI(seenIdx);
    int ch   = WiFi.channel(seenIdx);
    wifi_auth_mode_t auth = WiFi.encryptionType(seenIdx);
    const char* authName = "UNKNOWN";
    switch (auth) {
      case WIFI_AUTH_OPEN:            authName = "OPEN";            break;
      case WIFI_AUTH_WEP:             authName = "WEP";             break;
      case WIFI_AUTH_WPA_PSK:         authName = "WPA-PSK";         break;
      case WIFI_AUTH_WPA2_PSK:        authName = "WPA2-PSK";        break;
      case WIFI_AUTH_WPA_WPA2_PSK:    authName = "WPA/WPA2-PSK";    break;
      case WIFI_AUTH_WPA2_ENTERPRISE: authName = "WPA2-ENTERPRISE"; break;
      case WIFI_AUTH_WPA3_PSK:        authName = "WPA3-PSK";        break;
      case WIFI_AUTH_WPA2_WPA3_PSK:   authName = "WPA2/WPA3-PSK";   break;
      default: break;
    }
    Serial.printf("[WiFi] target AP: ssid='%s' ch=%d rssi=%d dBm auth=%s\n",
                  WIFI_SSID, ch, rssi, authName);
    if (rssi < -80) {
      Serial.println("[WiFi]   ⚠ segnale debole (< -80 dBm) — avvicina il device");
    }
    if (auth == WIFI_AUTH_WPA3_PSK) {
      Serial.println("[WiFi]   ⚠ AP solo WPA3: alcuni hotspot iOS lo impostano per default.");
      Serial.println("[WiFi]     Soluzione: nelle impostazioni hotspot abilita compatibilità WPA2,");
      Serial.println("[WiFi]     oppure aggiorna il core arduino-esp32 alla 3.x se è già su WPA3.");
    } else {
      Serial.printf("[WiFi]   → AP in %s: la causa più probabile è la PASSWORD.\n", authName);
      Serial.println("[WiFi]     Verifica esatta: maiuscole/minuscole, ! finale, niente spazi.");
      Serial.println("[WiFi]     Prova a connettere uno smartphone alla stessa SSID con quella password.");
    }
  } else {
    Serial.printf("[WiFi] '%s' NON visibile a 2.4 GHz.\n", WIFI_SSID);
    Serial.println("[WiFi]   → iPhone hotspot: Impostazioni > Hotspot personale > 'Massima compatibilità' = ON");
    Serial.println("[WiFi]   → Android hotspot: imposta banda 2.4 GHz (non Auto/5 GHz)");
    Serial.println("[WiFi]   → router casa: verifica che 2.4 GHz sia attivo (alcuni mesh ne usano solo 5 GHz)");
  }
  WiFi.scanDelete();
}

// Bring-up bloccante: resetta lo stato STA, applica i tuning
// consigliati (no sleep, auto-reconnect, no flash writes, hostname)
// e attende fino a `timeoutMs` per WL_CONNECTED.
bool wifiBringUp(unsigned long timeoutMs) {
  Serial.printf("[WiFi] Connecting to '%s' ...\n", WIFI_SSID);
  WiFi.disconnect(true, true);
  delay(50);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.setHostname("replica-mod-01");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  bool ok = (WiFi.status() == WL_CONNECTED);
  if (ok) {
    Serial.printf("\n[WiFi] OK  ip=%s  rssi=%d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    // Codici utili: 1=NO_SSID_AVAIL, 4=CONNECT_FAILED, 6=DISCONNECTED.
    Serial.printf("\n[WiFi] FAIL  status=%d  (1=NO_SSID, 4=BAD_PW, 6=DISCONN)\n",
                  (int)WiFi.status());
    wifiScanReport();
  }
  return ok;
}

// Da chiamare a ogni iterazione del loop. Non bloccante a riposo;
// quando rileva un drop, tenta un singolo bring-up e poi torna.
void wifiHousekeep() {
  unsigned long now = millis();
  bool nowUp = (WiFi.status() == WL_CONNECTED);

  // Transizione UP → DOWN: aggiorna display una volta sola.
  if (s_wifiUp && !nowUp) {
    s_wifiUp = false;
    Serial.println("[WiFi] link DOWN — will retry");
    drawBootScreen("WIFI LOST", "retrying...");
  }
  // Transizione DOWN → UP: aggiorna display una volta sola.
  if (!s_wifiUp && nowUp) {
    s_wifiUp = true;
    Serial.printf("[WiFi] link UP  ip=%s\n", WiFi.localIP().toString().c_str());
    if (awaitingFirstSentence) {
      // drawAwaitingScreen mostra già REPLICA_ + CONNECTED + IP +
      // AWAITING in un'unica vista — niente schermata intermedia.
      redrawAwaitingFull();
    } else {
      // Post-awaiting: l'utente è già in UI principale, mostra l'IP
      // brevemente come conferma di riconnessione, poi torna alla UI.
      drawBootScreen("CONNECTED", WiFi.localIP().toString().c_str());
      delay(1500);
      redrawAllFull(renderedCatIdx, renderedMode);
    }
  }

  if (nowUp) return;
  if (now - s_lastWifiCheckMs < WIFI_CHECK_INTERVAL_MS) return;
  s_lastWifiCheckMs = now;
  wifiBringUp(WIFI_BRINGUP_TIMEOUT_MS);
}

// ─────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(ENC1_CLK, INPUT_PULLUP);
  pinMode(ENC1_DT,  INPUT_PULLUP);
  pinMode(ENC1_SW,  INPUT_PULLUP);
  pinMode(ENC2_CLK, INPUT_PULLUP);
  pinMode(ENC2_DT,  INPUT_PULLUP);

  // Encoder 1: decoder quadratura su entrambi i fronti di CLK e DT
  // (precisione massima, nessuno skip). Il pulsante resta su FALLING.
  enc1PrevState = (digitalRead(ENC1_CLK) << 1) | digitalRead(ENC1_DT);
  attachInterrupt(digitalPinToInterrupt(ENC1_CLK), enc1ISR,   CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC1_DT),  enc1ISR,   CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC1_SW),  enc1SwISR, FALLING);
  // Encoder 2: gli interrupt vengono attaccati ALLA FINE di setup(),
  // dopo WiFi + e-ink, così durante l'inizializzazione (lenta e rumorosa)
  // nessun sub-step può accumularsi e la posizione resta garantita a 0.

  // LED esterno: forziamo OFF il prima possibile.
  // - digitalWrite(LOW) PRIMA di analogWrite ci dà uno 0 V certo sui pin
  //   anche se il LEDC PWM non è ancora attivato.
  // - lastLedColor viene riallineato a COL_OFF così applySteadyLED(0) più
  //   tardi non confonderà uno scrivere "stato uguale" con un noop.
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  digitalWrite(LED_R, LED_COMMON_ANODE ? HIGH : LOW);
  digitalWrite(LED_G, LED_COMMON_ANODE ? HIGH : LOW);
  digitalWrite(LED_B, LED_COMMON_ANODE ? HIGH : LOW);
  writeLED(COL_OFF);
  lastLedColor = COL_OFF;

  // Nano ESP32 ha un RGB LED on-board (LED_RED/GREEN/BLUE, common-anode:
  // HIGH = spento). Se non lo spegniamo esplicitamente, all'avvio resta
  // verde finché il bootloader non rilascia i pin.
#ifdef LED_RED
  pinMode(LED_RED,   OUTPUT); digitalWrite(LED_RED,   HIGH);
#endif
#ifdef LED_GREEN
  pinMode(LED_GREEN, OUTPUT); digitalWrite(LED_GREEN, HIGH);
#endif
#ifdef LED_BLUE
  pinMode(LED_BLUE,  OUTPUT); digitalWrite(LED_BLUE,  HIGH);
#endif

  display.init(115200);
  display.setRotation(3);

  drawBootScreen("Connecting...", WIFI_SSID);

  // Sanity scan al boot: prova la radio prima di qualsiasi begin().
  // Se anche questo scan ritorna -2/0 abbiamo conferma che è un
  // problema HW (alimentazione/cavo/board) e non di credenziali.
  Serial.println("[WiFi] boot sanity scan...");
  WiFi.mode(WIFI_STA);
  delay(200);
  int bootScan = WiFi.scanNetworks();
  Serial.printf("[WiFi] boot scan rc=%d\n", bootScan);
  if (bootScan > 0) {
    for (int i = 0; i < bootScan; i++) {
      Serial.printf("  %2d) rssi=%4d dBm  ssid='%s'\n",
                    i + 1, WiFi.RSSI(i), WiFi.SSID(i).c_str());
    }
    WiFi.scanDelete();
  }

  // Primo tentativo di bring-up. Se fallisce non blocchiamo per sempre:
  // il loop principale continuerà a riprovare via wifiHousekeep().
  // Su SUCCESS non disegniamo uno schermo intermedio "CONNECTED + IP":
  // redrawAwaitingFull() più sotto mostra REPLICA_ + CONNECTED + IP +
  // AWAITING in un'unica vista top-left aligned.
  s_wifiUp = wifiBringUp(WIFI_BRINGUP_TIMEOUT_MS);
  if (!s_wifiUp) {
    drawBootScreen("WIFI FAILED", "retrying...");
    delay(3000);
  }

  server.on("/status",   HTTP_GET,     handleStatus);
  server.on("/status",   HTTP_OPTIONS, handleOptions);
  server.on("/sentence", HTTP_POST,    handleSentencePost);
  server.on("/sentence", HTTP_OPTIONS, handleOptions);
  server.on("/action",   HTTP_GET,     handleActionGet);
  server.on("/action",   HTTP_OPTIONS, handleOptions);

  server.begin();
  Serial.println("HTTP server running on port 80");

  // ── OTA: upload sketch via WiFi from the Arduino IDE ──────
  // Once this runs, the board appears under Tools → Port as a
  // "Network Port" named "replica-mod-01 at <ip>". Select it and
  // hit Upload to flash over WiFi instead of USB.
  ArduinoOTA.setHostname("replica-mod-01");
  // Uncomment to require a password on every upload:
  // ArduinoOTA.setPassword("replica");
  ArduinoOTA.onStart([]() {
    tlog("[OTA] start\n");
    drawBootScreen("OTA UPLOAD", "in progress...");
  });
  ArduinoOTA.onEnd([]() {
    tlog("\n[OTA] end — rebooting\n");
  });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    tlog("[OTA] %u%%\r", (p / (t / 100)));
  });
  ArduinoOTA.onError([](ota_error_t e) {
    tlog("[OTA] error[%u]\n", e);
  });
  ArduinoOTA.begin();
  tlog("[OTA] ready  host=replica-mod-01.local  ip=%s\n",
       WiFi.localIP().toString().c_str());

  // Telnet log server: connetti da Terminale con `telnet <ip>`
  // o `nc <ip> 23` per vedere i log via WiFi.
  telnetSrv.begin();
  telnetSrv.setNoDelay(true);
  tlog("[TELNET] log server on port 23 — `telnet %s` to view logs\n",
       WiFi.localIP().toString().c_str());

  currentCategory = CAT_KEYS[0];
  currentMode     = modeStr(0);
  renderedCatIdx  = 0;
  renderedMode    = 0;
  // Boot: nessuna frase ancora ricevuta dall'estensione → schermata
  // "AWAITING CONNECTION..." pulita, senza categoria/command line.
  redrawAwaitingFull();
  applySteadyLED(0);

  // Boot = "appena mosso", così la selezione categoria è visibile per i
  // primi ENC1_SELECTION_TIMEOUT_MS millisecondi.
  lastEnc1MoveMs = millis();

  // Encoder 2: ora che boot/WiFi/e-ink sono finiti, riporta la posizione
  // al centro (ENC2_BOOT_POS = neutral) e attacca finalmente gli interrupt.
  // Da qui in poi: CCW dal centro → amplify, CW dal centro → poison.
  enc2EncPos    = ENC2_BOOT_POS;
  enc2SubStep   = 0;
  enc2PrevState = (digitalRead(ENC2_CLK) << 1) | digitalRead(ENC2_DT);
  attachInterrupt(digitalPinToInterrupt(ENC2_CLK), enc2ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC2_DT),  enc2ISR, CHANGE);
  Serial.print("Enc2  -> pos=");
  Serial.print(ENC2_BOOT_POS);
  Serial.print("/");
  Serial.print(ENC2_POS_MAX);
  Serial.println("  mode=neutral (boot, center)");
}

// ─────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────
void loop() {
  wifiHousekeep();
  ArduinoOTA.handle();
  server.handleClient();

  // Telnet log: accetta UN client alla volta. Se ne arriva uno nuovo,
  // chiude il precedente (così un riavvio del client telnet riprende
  // a vedere log senza dover riavviare il device).
  if (telnetSrv.hasClient()) {
    if (telnetClient && telnetClient.connected()) telnetClient.stop();
    telnetClient = telnetSrv.accept();
    telnetClient.println("[TELNET] connected — live logs follow");
  }

  // ── Encoder 1: consuma detent emessi dall'ISR e fai dispatch ───
  // sul giusto target a seconda della UI state. Dopo dispatch resetta
  // il timeout di visibilità della selezione.
  int delta;
  noInterrupts();
  delta = enc1PendingDelta;
  enc1PendingDelta = 0;
  interrupts();
  if (delta != 0) {
    if (uiState == UI_ACTION_SELECT) {
      int v = intensityIdx + delta;
      v = ((v % N_INT) + N_INT) % N_INT;
      intensityIdx = v;
    } else if (uiState == UI_IDLE) {
      int v = enc1Pos + delta;
      v = ((v % N_CAT) + N_CAT) % N_CAT;
      enc1Pos = v;
    }
    // UI_UNDO ignora la rotazione (decisione binaria) ma il movimento
    // conta comunque per il timeout della selezione.
    // UI_FORCE_NEUTRAL: ignoriamo completamente — niente cambio di
    // selezione e niente reset del timeout, così la schermata di
    // blocco non viene mai "scrollata via".
    if (uiState != UI_FORCE_NEUTRAL) {
      lastEnc1MoveMs = millis();
    }
  }

  int catIdx = enc1Pos;

  // ── Encoder 2: posizione assoluta 0..10 → modo ──────────────
  // enc2EncPos è la posizione corrente (aggiornata dall'ISR a ogni
  // click, con hard-stop a 0 e a ENC2_POS_MAX — niente wrap).
  int encPos = enc2EncPos;
  int mode   = modeFromPos(encPos);
  enc2Pos    = mode;   // sincronizza per eventuali lettori esterni

  // Inizializzato a ENC2_BOOT_POS (lo stato di boot già stampato in setup),
  // così la prima iterazione del loop non duplica la riga di boot.
  static int lastLoggedEncPos = ENC2_BOOT_POS;
  if (encPos != lastLoggedEncPos) {
    lastLoggedEncPos = encPos;
    Serial.print("Enc2  -> pos=");
    Serial.print(encPos);
    Serial.print("/");
    Serial.print(ENC2_POS_MAX);
    Serial.print("  mode=");
    Serial.println(modeStr(mode));
  }

  // ── Encoder 2: transizione modo + state machine UI ───────────
  if (mode != renderedMode) {
    renderedMode = mode;
    currentMode  = modeStr(mode);
    Serial.print("Mode  -> ");
    Serial.println(currentMode);

    // Cambio di modo → cambio di UI state.
    if (uiState == UI_FORCE_NEUTRAL) {
      // Sblocco SOLO quando l'utente riporta davvero l'encoder a 0.
      // Se passa fra poison ↔ amplify, lo schermo di blocco resta.
      if (mode == 0) {
        uiState = UI_IDLE;
        renderedCatIdx = -1;  // partiamo con la categoria nascosta
        Serial.println("Force-neutral: released — UI unlocked");
        redrawAllFull(-1, 0);
      }
    } else if (uiState == UI_UNDO) {
      // In UI_UNDO il ritorno a neutral significa "accetto l'azione":
      // facciamo fire IMMEDIATO del commit deferred (così non si perde)
      // e usciamo a UI_IDLE. Swap poison↔amplify senza passare per
      // neutral: rimane UI_UNDO, deferred continua.
      if (mode == 0) {
        if (commitDeadlineMs > 0 && deferredAction.length() > 0) {
          pendingAction    = deferredAction;
          pendingCategory  = deferredCategory;
          pendingIntensity = deferredIntensity;
          Serial.print("Returned to neutral -> commit fired -> ");
          Serial.println(pendingAction);
          deferredAction = ""; deferredCategory = ""; deferredIntensity = 0;
          commitDeadlineMs = 0;
        }
        uiState = UI_IDLE;
        Serial.println("UNDO: returned to neutral, exiting");
      }
    } else {
      uiState = (mode == 0) ? UI_IDLE : UI_ACTION_SELECT;
    }
    if (ledAnim == LED_NONE) applySteadyLED(mode);
  }

  // ── Encoder 1: selezione visibile entro ENC1_SELECTION_TIMEOUT_MS ──
  // dall'ultimo movimento. Oltre, la categoria evidenziata sparisce
  // (displayedCatIdx = -1 → drawSentence non disegna nessun riquadro).
  // ECCEZIONE: in UI_ACTION_SELECT e UI_UNDO la categoria DEVE restare
  // visibile per tutta la durata dell'azione — l'utente deve sempre
  // vedere quale categoria sta poison/amplify-ando. Il timeout vale
  // quindi solo in UI_IDLE.
  bool selectionVisible =
       (millis() - lastEnc1MoveMs) < ENC1_SELECTION_TIMEOUT_MS
       || uiState == UI_ACTION_SELECT
       || uiState == UI_UNDO;
  int displayedCatIdx = selectionVisible ? catIdx : -1;
  if (displayedCatIdx != renderedCatIdx) {
    renderedCatIdx = displayedCatIdx;
    if (displayedCatIdx >= 0) {
      currentCategory = CAT_KEYS[displayedCatIdx];
      Serial.print("Cat   -> ");
      Serial.println(currentCategory);
    } else {
      Serial.println("Cat   -> (hidden, 10s idle)");
    }
    // Cambio selezione utente → partial refresh secco, niente animazione.
    // (Lo scramble è riservato ai cambi di previsione del sistema.)
    // In UI_FORCE_NEUTRAL non ridisegniamo: la rotazione di enc1 non
    // deve sovrascrivere la schermata di blocco.
    if (uiState != UI_FORCE_NEUTRAL) {
      redrawAllPartial(displayedCatIdx, mode);
    }
  }

  // ── Encoder 1 click: significato dipende dallo stato ─────────
  if (actionFresh) {
    actionFresh = false;
    if (uiState == UI_ACTION_SELECT) {
      // Commit DEFERRED: memorizziamo l'azione e apriamo la finestra di
      // HOLD_TO_CANCEL_MS. pendingAction viene impostato SOLO se la
      // finestra scade senza che l'utente tenga premuto il bottone per
      // tutto il tempo. Se l'utente annulla, l'estensione non vede mai
      // l'azione e la dashboard non la registra.
      deferredAction      = currentMode;       // "poison" o "amplify"
      deferredCategory    = currentCategory;
      deferredIntensity   = intensityIdx + 1;  // 1..3
      lastActionType      = deferredAction;
      lastActionCategory  = deferredCategory;
      lastActionIntensity = deferredIntensity;
      commitDeadlineMs    = millis() + HOLD_TO_CANCEL_MS;
      uiState             = UI_UNDO;
      Serial.print("Action deferred -> ");
      Serial.print(deferredAction); Serial.print(' ');
      Serial.print(deferredCategory); Serial.print(' ');
      Serial.println(deferredIntensity);
    } else if (uiState == UI_UNDO) {
      // Click singolo durante la finestra deferred: scarta l'azione.
      // L'estensione non vede mai pendingAction="poison/amplify",
      // quindi la dashboard non registra niente.
      if (actionMsgUntilMs == 0 && commitDeadlineMs > 0) {
        Serial.print("Action canceled (click) -> ");
        Serial.println(deferredCategory);
        deferredAction = ""; deferredCategory = ""; deferredIntensity = 0;
        commitDeadlineMs = 0;
        actionMsgText    = "ACTION REVOKED";
        actionMsgUntilMs = millis() + ACTION_MSG_MS;
      }
    } else if (uiState == UI_IDLE) {
      // Niente da committare in neutral. Mostra l'hint per ricordare
      // all'utente di muovere encoder 2 in POISON o AMPLIFY.
      hintExpiresMs = millis() + HINT_WINDOW_MS;
      Serial.println("Hint: enter action mode");
    } else if (uiState == UI_FORCE_NEUTRAL) {
      // Tutti i click sono ignorati finché l'utente non riporta
      // l'encoder 2 a neutrale.
      Serial.println("Click ignored: return encoder 2 to neutral first");
    }
  }

  // ── Deferred commit countdown in UI_UNDO ────────────────────
  // L'annullamento avviene su click singolo (gestito dal click handler
  // sopra). Qui controlliamo solo la scadenza del timer: se nessuno ha
  // cliccato per cancellare entro HOLD_TO_CANCEL_MS, l'azione deferred
  // viene davvero mandata all'estensione.
  // Niente messaggio "ACTION SENT" intermedio: andiamo SUBITO alla
  // schermata FORCE_NEUTRAL (che già contiene "ACTION SET." in alto),
  // così il modal non arriva con un secondo di ritardo.
  if (uiState == UI_UNDO && actionMsgUntilMs == 0
      && commitDeadlineMs > 0 && millis() >= commitDeadlineMs) {
    pendingAction      = deferredAction;
    pendingCategory    = deferredCategory;
    pendingIntensity   = deferredIntensity;
    Serial.print("Deferred commit fired -> ");
    Serial.print(pendingAction); Serial.print(' ');
    Serial.print(pendingCategory); Serial.print(' ');
    Serial.println(pendingIntensity);
    deferredAction = ""; deferredCategory = ""; deferredIntensity = 0;
    commitDeadlineMs = 0;
    if (mode == 0) {
      uiState = UI_IDLE;
    } else {
      uiState = UI_FORCE_NEUTRAL;
      redrawForceNeutralFull();
    }
  }

  // Fine messaggio ACTION CANCELED → uscita da UI_UNDO. (ACTION SENT
  // non esiste più: il commit transita immediatamente al modal.)
  if (uiState == UI_UNDO && actionMsgUntilMs > 0
      && millis() >= actionMsgUntilMs) {
    actionMsgUntilMs = 0;
    actionMsgText    = "";
    uiState = (mode == 0) ? UI_IDLE : UI_ACTION_SELECT;
    Serial.println("Cancel message done");
  }

  // Debug print quando l'intensità cambia in action select.
  static int lastPrintedIntensity = -1;
  if (uiState == UI_ACTION_SELECT && intensityIdx != lastPrintedIntensity) {
    lastPrintedIntensity = intensityIdx;
    Serial.print("Intensity -> "); Serial.println(INT_LABELS[intensityIdx]);
  }

  // ── Ridisegna SOLO la command line quando il suo contenuto cambia.
  String sig = cmdLineSignature(mode, intensityIdx);
  if (sig != renderedCmdSignature) {
    redrawCommandLineOnly(mode);
  }

  // ── Decontext blink della/e parola/e nella frase ─────────────
  // Finché ci sono parole decontestualizzate, alterniamo il
  // dcBlinkState e facciamo un partial refresh dell'area frase.
  // Lo stop arriva da solo quando il prossimo POST /sentence
  // azzera decontextCount (dopo poison/amplify).
  // In UI_FORCE_NEUTRAL non disegnamo niente: la schermata di blocco
  // copre l'area frase.
  if (decontextCount > 0 && uiState != UI_FORCE_NEUTRAL) {
    if (millis() >= dcBlinkNextMs) {
      dcBlinkState = !dcBlinkState;
      dcBlinkNextMs = millis() + DC_BLINK_INTERVAL_MS;
      // Partial refresh dell'area frase (+1 px per includere la riga
      // separatrice in y=SENT_H, ridisegnata da drawSentence).
      display.setRotation(3);
      display.setPartialWindow(0, 0, SCREEN_W, SENT_H + 1);
      display.firstPage();
      do { drawSentence(renderedCatIdx); } while (display.nextPage());
    }
  } else if (dcBlinkState) {
    // Nessun decontext attivo ma siamo rimasti con il flag a true
    // (es. ultima parola appena ridipinta in stato "blink on"): reset.
    dcBlinkState = false;
  }

  tickLedAnim(mode);

  delay(20);
}
