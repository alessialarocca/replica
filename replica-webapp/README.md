# Replica_ — Dashboard webapp

## Setup in 4 passi

### 1. Installa l'estensione (una volta sola)
- Apri `chrome://extensions`
- Attiva **Developer mode** (in alto a destra)
- Clicca **Load unpacked** e seleziona la cartella `replica-extension`

### 2. Avvia la webapp
Apri il Terminale, entra nella cartella `replica-webapp` ed esegui:

```bash
./start.sh
```

Oppure manualmente:
```bash
cd replica-webapp
python3 -m http.server 8000
```

### 3. Apri nel browser
Vai su → `http://localhost:8000`

Il content script dell'estensione inietta automaticamente l'ID nel DOM
della pagina. Non devi copiare o incollare nulla.

### 4. Usa
- La dashboard si aggiorna ogni 8 secondi con i dati reali di navigazione
- Clicca su un vocable nella frase (o su una card) per aprire il drawer azione
- Scegli intensità 1/2/3 e poi POISON o AMPLIFY

## Note
- La webapp funziona solo su `localhost` (Chrome non permette
  `externally_connectable` su file://)
- I dati restano nel browser locale, non escono mai
- Il server locale serve solo i file statici, nessun backend
