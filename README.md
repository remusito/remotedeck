# RemoteDeck

Cliente ligero de Remote Play para PS5/PS4 pensado para el Steam Deck, al estilo de
[chiaki-ng](https://github.com/streetpea/chiaki-ng) / chiaki4deck.

- Motor del protocolo: `libchiaki` de chiaki-ng (submódulo en `third-party/chiaki-ng`).
- Frontend propio en C + SDL2: vídeo (FFmpeg, VA-API en el Deck), audio Opus, mando,
  giroscopio, vibración, pantalla táctil como touchpad, encendido remoto de la consola.

## Estructura

| Archivo | Qué hace |
|---|---|
| [src/main.c](src/main.c) | Comandos: `discover`, `register`, `list`, `wake`, `play`, `remove` |
| [src/discover.c](src/discover.c) | Búsqueda de consolas en la LAN y wakeup |
| [src/regist.c](src/regist.c) | Vinculación con PIN |
| [src/stream.c](src/stream.c) | Sesión: decodificación, render SDL, audio, eventos |
| [src/input.c](src/input.c) | Mando/teclado/táctil → estado DualSense |
| [src/config.c](src/config.c) | Consolas registradas en `~/.config/remotedeck/hosts.ini` |

## Instalación en Steam Deck

En **Modo Escritorio**, abre Konsole:

```bash
git clone --recursive <url-de-tu-repo> ~/remotedeck   # o copia esta carpeta
cd ~/remotedeck
bash scripts/steamdeck-setup.sh
```

El script crea un contenedor Arch con distrobox, compila y deja el ejecutable en `~/.local/bin/remotedeck`.

## Primer uso

1. **En la consola**: Ajustes → Sistema → Uso a distancia → activar *Habilitar uso a distancia*.
   Para poder encenderla desde reposo: Ajustes → Sistema → Ahorro de energía → Funciones disponibles en modo reposo → *Permanecer conectado a Internet* y *Habilitar el encendido de la PS5 desde la red*.
2. **Tu PSN Account ID** (base64, 8 bytes). Puedes obtenerlo con el script de chiaki-ng:
   ```bash
   python3 third-party/chiaki-ng/scripts/psn-account-id.py
   ```
3. **Buscar la consola**:
   ```bash
   remotedeck discover
   ```
4. **Vincular**: en la consola ve a *Uso a distancia → Vincular dispositivo* y usa el PIN de 8 dígitos:
   ```bash
   remotedeck register --host 192.168.1.50 --pin 12345678 --account-id 'AbCdEfGhIjk='
   ```
5. **Jugar**:
   ```bash
   remotedeck play            # despierta la consola si está en reposo y conecta
   remotedeck play --res 1080 --bitrate 15000
   ```

### Modo Juego

Steam → *Añadir un juego que no es de Steam* → selecciona `scripts/remotedeck-launch.sh`.
En las propiedades del acceso puedes poner opciones de lanzamiento (`--res 1080`, nombre de la consola…).
El log queda en `~/.local/state/remotedeck.log`.

## Controles

| Steam Deck | PS5 |
|---|---|
| A / B / X / Y | ✕ / ○ / □ / △ |
| View / Menú | Crear (Share) / Options |
| L4 (trasero) | Botón PS |
| R4 (trasero) | Clic de touchpad |
| Pantalla táctil | Touchpad |
| Giroscopio | Giroscopio (si Steam Input lo expone) |
| **L1 + R1 + View + Menú** | Salir del streaming |

Teclado (escritorio): Enter ✕, Retroceso ○, `[` □, `]` △, flechas, Q/E = L1/R1, Z/C = L2/R2,
O = Options, F1 = PS, F2 = touchpad, F3 = Share, F11 = pantalla completa, Shift+Esc = salir.

> En Modo Juego, Steam captura el botón Steam y los traseros según la plantilla de Steam Input.
> Si L4/R4 no llegan, asígnalos en la plantilla del juego a los botones "L4/R4" del mando o a F1/F2.

## Compilar en otro Linux (Ubuntu 24.04)

```bash
sudo apt install build-essential cmake ninja-build pkg-config python3-protobuf protobuf-compiler \
  libssl-dev libopus-dev libsdl2-dev libavcodec-dev libavutil-dev libjson-c-dev \
  libminiupnpc-dev libevent-dev libva-dev nasm
git submodule update --init --depth 1 third-party/chiaki-ng
git -C third-party/chiaki-ng submodule update --init --depth 1 \
  third-party/nanopb third-party/jerasure third-party/gf-complete third-party/curl
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

El workflow [.github/workflows/build.yml](.github/workflows/build.yml) hace lo mismo en GitHub Actions.

## Licencia

Al enlazar `libchiaki` (AGPL-3.0), RemoteDeck también se distribuye bajo **AGPL-3.0**.
Proyecto no oficial, sin relación con Sony Interactive Entertainment.
