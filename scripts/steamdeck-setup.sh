#!/usr/bin/env bash
# Compila e instala RemoteDeck en un Steam Deck (Modo Escritorio, Konsole).
# SteamOS es de solo lectura, así que compilamos dentro de un contenedor Arch
# con distrobox (incluido en SteamOS 3.5+) y exportamos el binario a ~/.local/bin.
set -euo pipefail

BOX=remotedeck-build
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"

if ! command -v distrobox >/dev/null; then
	echo "distrobox no encontrado. Actualiza SteamOS (3.5+) o instálalo:"
	echo "  curl -s https://raw.githubusercontent.com/89luca89/distrobox/main/install | sh -s -- --prefix ~/.local"
	exit 1
fi

if ! distrobox list | grep -qw "$BOX"; then
	echo ">> Creando contenedor Arch '$BOX'..."
	distrobox create --yes --name "$BOX" --image docker.io/library/archlinux:latest
fi

echo ">> Instalando dependencias y compilando..."
distrobox enter "$BOX" -- bash -euo pipefail -c "
	sudo pacman -Syu --noconfirm --needed base-devel cmake ninja git pkgconf python python-protobuf protobuf \
		sdl2 ffmpeg opus openssl json-c miniupnpc libevent nasm libva-mesa-driver mesa
	cd '$REPO_DIR'
	git submodule update --init --depth 1 third-party/chiaki-ng
	git -C third-party/chiaki-ng submodule update --init --depth 1 \
		third-party/nanopb third-party/jerasure third-party/gf-complete third-party/curl
	cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
	cmake --build build
	distrobox-export --bin '$REPO_DIR/build/remotedeck' --export-path \"\$HOME/.local/bin\"
"

echo
echo ">> Listo: ~/.local/bin/remotedeck"
echo "   1) remotedeck discover"
echo "   2) remotedeck register --host IP --pin PIN --account-id TU_ID"
echo "   3) Añade scripts/remotedeck-launch.sh a Steam como juego no-Steam para usarlo en Modo Juego."
