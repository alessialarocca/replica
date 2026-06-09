// ─────────────────────────────────────────────────────────────
// REPLICA_ — Arduino Nano ESP32 firmware v2.2
// HTTP server: riceve la frase + parole decontestualizzate +
// mappa categoria→vocabolo dall'estensione Chrome. Sul display
// e-ink mostra SOLO la frase: la parola della categoria
// selezionata (encoder 1) viene riquadrata in nero. Il LED
// segnala il modo (encoder 2). Su ogni nuova decontestualizzazione
// il LED fa due lampi viola e poi torna al colore del modo.
//
// Mappatura LED a regime:
//   ORANGE → modo POISON
//   GREEN  → modo AMPLIFY
//   OFF    → modo NEUTRAL
// Su nuova decontestualizzazione → 2 lampi PURPLE → ritorno a regime.
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
// ─────────────────────────────────────────────────────────────

#include <WiFi.h>
#include <WebServer.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMono9pt7b.h>
#include <ArduinoJson.h>
#include <SPI.h>

// ── Font ─────────────────────────────────────────────────────
// Frase: FreeMono9pt7b — font monospaziato incluso in Adafruit GFX.
// Command line e boot screen: bitmap 5x7 built-in con setTextSize(1).

// ── Credenziali WiFi ─────────────────────────────────────────
const char* WIFI_SSID     = "TIMDAISY";
const char* WIFI_PASSWORD = "Kobe2019!";

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
const RGB COL_PURPLE = {  60,  10,  90 };
const RGB COL_ORANGE = {  90,  25,   0 };
const RGB COL_GREEN  = {   0,  90,  20 };
const RGB COL_OFF    = {   0,   0,   0 };

// ── Server HTTP ──────────────────────────────────────────────
WebServer server(80);

// ── Categorie (devono combaciare con CAT_ORDER nel webapp) ───
const char* CAT_KEYS[]   = { "bio",  "geo",  "prof",  "econ", "socio", "psycho" };
const int   N_CAT        = 6;

// Posizione di ciascuna categoria nella frase costruita dall'estensione:
// "IDENTIFIED AS [bio], WORKING AS [prof], LOCATED IN [geo],
//  NETWORKED WITHIN [socio], VALUED AS [econ], AND EXHIBITING [psycho]."
// Serve a disambiguare quale "[?]" evidenziare quando il vocabolo della
// categoria selezionata è vuoto e ce ne sono altre vuote nella frase.
const int CAT_SENTENCE_POS[N_CAT] = { 0, 2, 1, 4, 3, 5 };

// ── Stato globale ────────────────────────────────────────────
String currentSentence = "AWAITING\nCONNECTION...";

// Vocabolo corrente per ogni categoria (uppercase). Vuoto = nessun dato.
String catVocables[N_CAT];

// Encoder 1 — indice categoria selezionata (0..N_CAT-1)
volatile int  enc1Pos = 0;
// Encoder 2 — stato azione: -1 = poison, 0 = neutral, +1 = amplify
volatile int  enc2Pos = 0;
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

// Stato animazione LED — 2 lampi viola: ON-OFF-ON-OFF (4 step da 250 ms)
enum LedAnim { LED_NONE, LED_BLINK_PURPLE };
LedAnim       ledAnim       = LED_NONE;
int           ledAnimStep   = 0;
unsigned long ledAnimNextMs = 0;
const unsigned long LED_BLINK_HALF_MS = 250;

// Ultimo colore LED applicato a regime
RGB           lastLedColor = { 1, 1, 1 };

// ── UI state machine ─────────────────────────────────────────
//   UI_IDLE          → encoder 1 rota tra categorie, command line vuota
//   UI_ACTION_SELECT → encoder 2 è in POISON/AMPLIFY: encoder 1 rota tra
//                      LOW/MID/HIGH; command line "POISON LOW MID HIGH"
//                      con la voce selezionata sottolineata.
//   UI_UNDO          → dopo un click in ACTION_SELECT: command line
//                      "UNDO Ns" che conta alla rovescia per 5 s.
//                      Un altro click annulla l'azione, altrimenti la
//                      command line torna ad ACTION_SELECT / IDLE.
enum UiState { UI_IDLE, UI_ACTION_SELECT, UI_UNDO };
volatile UiState uiState   = UI_IDLE;
volatile int     intensityIdx = 1;   // default = MID

// Ultima azione applicata (per /action → undo lato estensione)
String        lastActionType     = "";   // "poison" | "amplify"
String        lastActionCategory = "";
int           lastActionIntensity = 0;

// Coda /action: cosa segnalare nel prossimo poll
String        pendingAction      = "";   // "" = nessuna azione pendente
String        pendingCategory    = "";
int           pendingIntensity   = 0;

// Timer countdown undo (millis)
unsigned long undoExpiresMs      = 0;
const unsigned long UNDO_WINDOW_MS = 5000;

// Stato del command line attualmente disegnato — per evitare
// partial-refresh inutili.
String renderedCmdSignature = "";

// Decontext blink: ogni BLINK_INTERVAL alterniamo highlight nero ↔
// normale sulle parole decontestualizzate, finché decontextCount > 0
// (cioè finché l'utente non applica un'azione o l'estensione non
// cancella il flag).
bool          dcBlinkState  = false;
unsigned long dcBlinkNextMs = 0;
const unsigned long DC_BLINK_INTERVAL_MS = 1800;

// "Enter action mode" hint quando l'utente clicca encoder 1 in
// UI_IDLE (encoder 2 = neutral, nessuna azione da committare).
unsigned long hintExpiresMs = 0;
const unsigned long HINT_WINDOW_MS = 2200;

// Lista parole decontestualizzate (uppercase) ricevute dall'ultimo
// POST /sentence. È quella che già usavamo per il blink del LED.
String        decontextWords[8];

// ─────────────────────────────────────────────────────────────
// Encoder ISRs
// ─────────────────────────────────────────────────────────────
void IRAM_ATTR enc1ClkISR() {
  static unsigned long last = 0;
  unsigned long t = millis();
  if (t - last < 5) return;
  last = t;
  int delta = (digitalRead(ENC1_DT) == HIGH) ? 1 : -1;
  // In UI_IDLE → categoria. In UI_ACTION_SELECT → intensità.
  // In UI_UNDO la rotazione viene ignorata (la decisione è binaria).
  if (uiState == UI_ACTION_SELECT) {
    intensityIdx = (intensityIdx + delta + N_INT) % N_INT;
  } else if (uiState == UI_IDLE) {
    enc1Pos = (enc1Pos + delta + N_CAT) % N_CAT;
  }
}

void IRAM_ATTR enc1SwISR() {
  static unsigned long last = 0;
  unsigned long t = millis();
  if (t - last < 200) return;
  last = t;
  actionFresh = true;
}

void IRAM_ATTR enc2ClkISR() {
  static unsigned long last = 0;
  unsigned long t = millis();
  if (t - last < 5) return;
  last = t;
  if (digitalRead(ENC2_DT) == HIGH) {
    if (enc2Pos < 1)  enc2Pos++;
  } else {
    if (enc2Pos > -1) enc2Pos--;
  }
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
}

// Colore LED a regime: solo modo (no decontext qui — il decontext
// è solo un blink temporaneo, non uno stato persistente).
RGB steadyColor(int mode) {
  if (mode > 0) return COL_GREEN;
  if (mode < 0) return COL_ORANGE;
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
  ledAnim       = LED_BLINK_PURPLE;
  ledAnimStep   = 0;
  ledAnimNextMs = millis();
}

// Avanza il blink (2 lampi). Step 0,2 = ON viola; 1,3 = OFF.
void tickLedAnim(int mode) {
  if (ledAnim == LED_NONE) return;
  unsigned long now = millis();
  if (now < ledAnimNextMs) return;
  if (ledAnimStep >= 4) {
    ledAnim = LED_NONE;
    applySteadyLED(mode);
    return;
  }
  RGB c = (ledAnimStep % 2 == 0) ? COL_PURPLE : COL_OFF;
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

// Disegna la frase con AeonikMono6pt7b (monospaziato, ~6pt).
// Tre stati visivi per parola:
//  - SELEZIONATA (encoder 1) → sfondo nero stabile
//  - DECONTESTUALIZZATA      → sfondo nero solo se dcBlinkState (lampeggio)
//  - resto                   → testo nero su bianco
void drawSentence(int catIdx) {
  display.fillScreen(GxEPD_WHITE);
  display.setFont(&FreeMono9pt7b);
  display.setTextWrap(false);
  display.setTextColor(GxEPD_BLACK);

  // Metriche di FreeMono9pt7b (mono → larghezza fissa)
  const int CHAR_W = 11;     // xAdvance uniforme
  const int LINE_H = 15;     // un po' meno di yAdvance (18) per stare nei 102 px
  const int ASCENT = 9;      // altezza cassa maiuscola sopra baseline
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

    bool blackBg = inSelection || (inDecontext && dcBlinkState);

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
    display.setCursor(cursorX, cursorY);
    display.print(words[i]);
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
  if (uiState == UI_UNDO) {
    return String("U");
  }
  if (uiState == UI_ACTION_SELECT) {
    return String(mode > 0 ? "A:" : "P:") + String(intIdx);
  }
  // UI_IDLE: hint "ENTER ACTION MODE" finché hintExpiresMs è nel futuro
  if (hintExpiresMs > millis()) return "H";
  return "";
}

// Disegna SOLO la riga in basso. Usa il font built-in setTextSize(1)
// (~6pt) perché il testo è lungo; la sottolineatura è una hline.
void drawCommandLine(int mode, int intIdx) {
  const int Y_TOP    = SENT_H + 2;        // 104
  const int Y_TEXT   = Y_TOP + 5;          // baseline-ish per default font
  const int CHAR_W   = 6;
  const int MARGIN   = 6;

  display.fillRect(0, SENT_H, SCREEN_W, SCREEN_H - SENT_H, GxEPD_WHITE);
  display.drawFastHLine(0, SENT_H, SCREEN_W, GxEPD_BLACK);
  display.setFont(NULL);            // bitmap built-in 5x7
  display.setTextSize(1);
  display.setTextColor(GxEPD_BLACK, GxEPD_WHITE);
  display.setTextWrap(false);

  if (uiState == UI_UNDO) {
    display.setCursor(MARGIN, Y_TEXT);
    display.print("UNDO 5 SEC  (CLICK TO REVERT)");
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
    display.print("  ");
    // Tre opzioni con sottolineatura sulla selezionata
    int x = MARGIN + (int)(strlen(head) + 2) * CHAR_W;
    for (int i = 0; i < N_INT; i++) {
      const char* w = INT_LABELS[i];
      display.setCursor(x, Y_TEXT);
      display.print(w);
      int wlen = (int)strlen(w) * CHAR_W;
      if (i == intIdx) {
        display.drawFastHLine(x, Y_TEXT + 9, wlen, GxEPD_BLACK);
      }
      x += wlen + CHAR_W;   // spazio fra le opzioni
    }
    return;
  }
  // UI_IDLE: lascia bianca la riga
}

// Full refresh — chiamato al cambio frase (resetta ghosting)
void redrawAllFull(int catIdx, int mode) {
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
  display.setRotation(3);
  display.setPartialWindow(0, 0, SCREEN_W, SCREEN_H);
  display.firstPage();
  do {
    drawSentence(catIdx);
    drawCommandLine(mode, intensityIdx);
  } while (display.nextPage());
  renderedCmdSignature = cmdLineSignature(mode, intensityIdx);
}

// Partial refresh della sola command line — usato durante
// UI_ACTION_SELECT (cambio intensità) e UI_UNDO (countdown).
void redrawCommandLineOnly(int mode) {
  display.setRotation(3);
  display.setPartialWindow(0, SENT_H, SCREEN_W, SCREEN_H - SENT_H);
  display.firstPage();
  do { drawCommandLine(mode, intensityIdx); } while (display.nextPage());
  renderedCmdSignature = cmdLineSignature(mode, intensityIdx);
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

  // Mappa categoria → vocabolo
  JsonObject vocObj = doc["vocables"].as<JsonObject>();
  Serial.print("vocables ->");
  for (int i = 0; i < N_CAT; i++) {
    catVocables[i] = "";
    if (!vocObj.isNull()) {
      const char* v = vocObj[CAT_KEYS[i]];
      if (v) {
        String s = String(v);
        s.toUpperCase();
        catVocables[i] = s;
      }
    }
    Serial.print(' '); Serial.print(CAT_KEYS[i]); Serial.print('=');
    Serial.print(catVocables[i].length() ? catVocables[i] : String("?"));
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
  bool changedAndNonEmpty = (newSig != decontextSignature) && (decontextCount > 0);
  decontextSignature = newSig;

  server.send(200, "application/json", "{\"ok\":true}");
  // Blink viola SINCRONO prima del redraw, così il LED reagisce
  // subito anche con encoder 2 in POISON/AMPLIFY — durante i 2 s di
  // full refresh la loop() resta bloccata e tickLedAnim non gira.
  if (changedAndNonEmpty) {
    for (int i = 0; i < 2; i++) {
      writeLED(COL_PURPLE);
      delay(250);
      writeLED(COL_OFF);
      delay(250);
    }
    applySteadyLED(renderedMode);
  }
  redrawAllFull(renderedCatIdx >= 0 ? renderedCatIdx : 0, renderedMode);
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
  display.setRotation(3);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK, GxEPD_WHITE);
    display.setTextSize(2);
    display.setCursor(6, 6);
    display.print("REPLICA_");
    display.setTextSize(1);
    display.setCursor(6, 36);
    display.print(line1);
    display.setCursor(6, 50);
    display.print(line2);
  } while (display.nextPage());
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

  attachInterrupt(digitalPinToInterrupt(ENC1_CLK), enc1ClkISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(ENC1_SW),  enc1SwISR,  FALLING);
  attachInterrupt(digitalPinToInterrupt(ENC2_CLK), enc2ClkISR, FALLING);

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  writeLED(COL_OFF);

  display.init(115200);
  display.setRotation(3);

  drawBootScreen("Connecting...", WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nIP: " + WiFi.localIP().toString());
    drawBootScreen("CONNECTED", WiFi.localIP().toString().c_str());
    delay(4000);
  } else {
    Serial.println("\nWiFi connection FAILED — check SSID/password");
    drawBootScreen("WIFI FAILED", WIFI_SSID);
    delay(6000);
  }

  server.on("/status",   HTTP_GET,     handleStatus);
  server.on("/status",   HTTP_OPTIONS, handleOptions);
  server.on("/sentence", HTTP_POST,    handleSentencePost);
  server.on("/sentence", HTTP_OPTIONS, handleOptions);
  server.on("/action",   HTTP_GET,     handleActionGet);
  server.on("/action",   HTTP_OPTIONS, handleOptions);

  server.begin();
  Serial.println("HTTP server running on port 80");

  currentCategory = CAT_KEYS[0];
  currentMode     = modeStr(0);
  renderedCatIdx  = 0;
  renderedMode    = 0;
  redrawAllFull(0, 0);
  applySteadyLED(0);
}

// ─────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────
void loop() {
  server.handleClient();

  int catIdx = enc1Pos;
  int mode   = enc2Pos;

  // ── Encoder 2: transizione modo + state machine UI ───────────
  if (mode != renderedMode) {
    renderedMode = mode;
    currentMode  = modeStr(mode);
    Serial.print("Mode  -> ");
    Serial.println(currentMode);

    // Cambio di modo → cambio di UI state (se non siamo in undo).
    if (uiState != UI_UNDO) {
      uiState = (mode == 0) ? UI_IDLE : UI_ACTION_SELECT;
    }
    if (ledAnim == LED_NONE) applySteadyLED(mode);
  }

  // ── Encoder 1 ruotato: in UI_IDLE cambia la categoria,
  //    in UI_ACTION_SELECT cambia la sola intensità.
  if (catIdx != renderedCatIdx) {
    renderedCatIdx  = catIdx;
    currentCategory = CAT_KEYS[catIdx];
    Serial.print("Cat   -> ");
    Serial.println(currentCategory);
    redrawAllPartial(catIdx, mode);
  }

  // ── Encoder 1 click: significato dipende dallo stato ─────────
  if (actionFresh) {
    actionFresh = false;
    if (uiState == UI_ACTION_SELECT) {
      // Commit azione → coda /action, transizione a UI_UNDO
      pendingAction     = currentMode;       // "poison" o "amplify"
      pendingCategory   = currentCategory;
      pendingIntensity  = intensityIdx + 1;  // 1..3
      lastActionType        = pendingAction;
      lastActionCategory    = pendingCategory;
      lastActionIntensity   = pendingIntensity;
      uiState         = UI_UNDO;
      undoExpiresMs   = millis() + UNDO_WINDOW_MS;
      Serial.print("Action commit -> ");
      Serial.print(pendingAction); Serial.print(' ');
      Serial.print(pendingCategory); Serial.print(' ');
      Serial.println(pendingIntensity);
    } else if (uiState == UI_UNDO) {
      // Annulla l'azione → coda /action con type "undo"
      pendingAction    = "undo";
      pendingCategory  = lastActionCategory;
      pendingIntensity = 0;
      uiState          = (mode == 0) ? UI_IDLE : UI_ACTION_SELECT;
      undoExpiresMs    = 0;
      Serial.print("Action undo  -> ");
      Serial.println(pendingCategory);
    } else if (uiState == UI_IDLE) {
      // Niente da committare in neutral. Mostra l'hint per ricordare
      // all'utente di muovere encoder 2 in POISON o AMPLIFY.
      hintExpiresMs = millis() + HINT_WINDOW_MS;
      Serial.println("Hint: enter action mode");
    }
  }

  // ── Scadenza finestra UNDO ───────────────────────────────────
  if (uiState == UI_UNDO && millis() >= undoExpiresMs) {
    uiState       = (mode == 0) ? UI_IDLE : UI_ACTION_SELECT;
    undoExpiresMs = 0;
    Serial.println("Undo window expired");
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
  if (decontextCount > 0) {
    if (millis() >= dcBlinkNextMs) {
      dcBlinkState = !dcBlinkState;
      dcBlinkNextMs = millis() + DC_BLINK_INTERVAL_MS;
      // Partial refresh della sola area frase
      display.setRotation(3);
      display.setPartialWindow(0, 0, SCREEN_W, SENT_H);
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
