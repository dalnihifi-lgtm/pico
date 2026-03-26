# CDI-NES Controller
Adattatore NES controller → Philips CD-i su Raspberry Pi Pico (RP2040)
Supporta due modalità: **cavo** (Mini-DIN 8) e **IR** (telecomando RC-5).

---

## Selezione modalità

In `main.c`, decommenta la riga desiderata:

```c
#define MODE_CABLE   // ← output via cavo Mini-DIN 8
// #define MODE_IR   // ← output via LED IR (RC-5)
```

---

## Schema elettrico — Modalità CAVO

```
NES Controller (7 pin)          RP2040 Pico
Pin 1 +5V  ──────────────────── VBUS
Pin 2 CLOCK ─────────────────── GPIO 11
Pin 3 LATCH ─────────────────── GPIO 10
Pin 4 DATA  ─────────────────── GPIO 12
Pin 7 GND  ──────────────────── GND

CD-i DIN 8                      RP2040 Pico
Pin 1 DATA ── [300Ω] ─────────── GPIO 0
             └─ [4.7kΩ] → VBUS (+5V)
Pin 2 GND  ──────────────────── GND
Pin 3 +5V  ──────────────────── VBUS
```

---

## Schema elettrico — Modalità IR

```
NES Controller (7 pin)          RP2040 Pico
(identico a sopra)

LED IR (es. TSAL6200)           RP2040 Pico
Anodo  ── [330Ω] ────────────── GPIO 1
Catodo ──────────────────────── GND
```

Nessun cavo verso il CD-i — il LED IR punta verso il sensore del player.

---

## Protocollo IR (RC-5 Philips)

| Parametro       | Valore        |
|-----------------|---------------|
| Portante        | 36 kHz        |
| Encoding        | Manchester    |
| Bit per frame   | 14            |
| Indirizzo CD-i  | 0x00          |

### Mappatura comandi RC-5

| NES      | Comando RC-5 | Valore |
|----------|--------------|--------|
| Su       | RC5_UP       | 0x01   |
| Giù      | RC5_DOWN     | 0x02   |
| Sinistra | RC5_LEFT     | 0x04   |
| Destra   | RC5_RIGHT    | 0x08   |
| A        | RC5_BTN1     | 0x35   |
| B        | RC5_BTN2     | 0x36   |
| START    | RC5_START    | 0x0C   |

> I codici RC-5 esatti potrebbero variare tra modelli CD-i.
> Verifica con un logic analyzer o un'app IR sul telefono se il player non risponde.

---

## Build

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
mkdir build && cd build
cmake ..
make -j4
```

Oppure usa il workflow GitHub Actions incluso (`.github/workflows/build.yml`).

---

## File del progetto

| File                            | Descrizione                        |
|---------------------------------|------------------------------------|
| `CMakeLists.txt`                | Build system CMake                 |
| `cdi_tx.pio`                    | State machine PIO per TX seriale   |
| `main.c`                        | Logica principale (cavo + IR)      |
| `README.md`                     | Questo file                        |
| `.github/workflows/build.yml`   | Build automatico su GitHub Actions |
