#!/bin/bash
# Replica_ — avvia la webapp in locale
echo ""
echo "  Rep1ica_"
echo "  ─────────────────────────────"
echo "  Dashboard in avvio su http://localhost:8000"
echo "  Premi Ctrl+C per fermare."
echo ""
cd "$(dirname "$0")"
python3 -m http.server 8000
