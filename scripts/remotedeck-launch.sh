#!/usr/bin/env bash
# Lanzador para Modo Juego: añádelo a Steam como "juego no-Steam".
# Las opciones de lanzamiento de Steam se pasan a 'remotedeck play'
# (p.ej.  --res 1080 --bitrate 15000  o el nombre de la consola).
LOG="${XDG_STATE_HOME:-$HOME/.local/state}/remotedeck.log"
mkdir -p "$(dirname "$LOG")"
exec "$HOME/.local/bin/remotedeck" play "$@" >"$LOG" 2>&1
