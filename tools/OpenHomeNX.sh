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

# Loop launcher: quando l'app esce, distingue i tre casi.
#  - l'app ha avviato un gioco (fork di RetroArch, poi esce): aspetta che
#    RetroArch finisca e RILANCIA l'app (si torna al menu dell'app, non a ES);
#  - l'app ha installato un aggiornamento (exit 42): rilancia UNA volta sola
#    (contatore anti-loop: se la nuova build riesce subito 42 di nuovo, non
#    si ripete all'infinito);
#  - l'utente ha chiuso l'app senza lanciare giochi: esce dal loop -> ES riprende.
relaunched=0
while true; do
  rc=0
  ./OpenHomeNX || rc=$?

  if [ $rc -eq 42 ] && [ $relaunched -eq 0 ]; then
    relaunched=1
    sleep 2
    continue
  fi

  if pgrep -x retroarch >/dev/null 2>&1; then
    for _ in $(seq 1 120); do
      if ! pgrep -x retroarch >/dev/null 2>&1; then break; fi
      sleep 1
    done
    sleep 2
    continue
  fi
  break
done