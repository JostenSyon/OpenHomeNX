#!/bin/bash
# OpenHomeNX.sh — avvio OpenHomeNX su ArkOS/R36S (Port nativo).
#
# Su ArkOS, i port SDL2 non richiedono systemctl stop: EmulationStation
# si mette in pausa automaticamente quando lancia questo script e riprende
# il controllo non appena ./OpenHomeNX termina.
set -e

export HOME=/home/ark
cd /home/ark/OpenHomeNX

# Env SDL per il driver GPU Mali (R36S)
export SDL_VIDEODRIVER=kmsdrm
export SDL_VIDEO_EGL_DRIVER=libmali.so

# Avvia l'app in foreground. Quando l'utente preme Esci nell'app,
# il processo termina e ES torna visibile automaticamente.
./OpenHomeNX