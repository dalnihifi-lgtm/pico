# CDI-NES Controller
Adattatore NES controller → Philips CD-i su Raspberry Pi Pico (RP2040)

---

## Schema elettrico

```
NES Controller (connettore 7 pin)       RP2040 Pico
┌─────────────────────┐                ┌──────────────────┐
│ Pin 1: +5V          ├────────────────┤ VBUS             │
│ Pin 2: CLOCK        ├────────────────┤ GPIO 11          │
│ Pin 3: LATCH        ├────────────────┤ GPIO 10          │
│ Pin 4: DATA (out)   ├────────────────┤ GPIO 12          │
│ Pin 5: n/c          │                │                  │
│ Pin 6: n/c          │                │                  │
│ Pin 7: GND          ├────────────────┤ GND              │
└─────────────────────┘                │                  │
                                       │ GPIO 0 ──[330Ω]──┼──┬── CD-i DIN Pin 1
CD-i Player (DIN 8)                    │                  │  └──[4.7kΩ]── +5V
┌─────────────────────┐                │ GND         ─────┼───── CD-i DIN Pin 2
│ Pin 1: DATA         │                │ VBUS (5V)   ─────┼───── CD-i DIN Pin 3
│ Pin 2: GND          │                └──────────────────┘
│ Pin 3: +5V          │
│ Pin 4..8: n/c       │
└─────────────────────┘
```

### Componenti necessari
| Componente       | Valore  | Note                                  |
|------------------|---------|---------------------------------------|
| Resistore R1     | 330 Ω   | Protezione GPIO su linea DATA CD-i    |
| Resistore R2     | 4.7 kΩ  | Pull-up a +5V su linea DATA CD-i      |
| Connettore NES   | 7 pin   | Femmina (lato Pico), maschio (controller) |
| Connettore DIN 8 | 8 pin   | Maschio (lato Pico → player)          |

> ⚠️ Il CD-i lavora a 5V. L'RP2040 è 3.3V.  
> Il pull-up esterno a 5V garantisce il livello logico corretto lato player.  
> I segnali LATCH/CLOCK dal Pico a 3.3V sono sufficienti per il 4021 del NES.

---

## Mappatura pulsanti

| NES        | CD-i              |
|------------|-------------------|
| Su         | Su                |
| Giù        | Giù               |
| Sinistra   | Sinistra          |
| Destra     | Destra            |
| A          | Button 1          |
| B          | Button 2          |
| START      | Button 1 + Button 2 |
| SELECT     | (non mappato)     |

---

## Build

### Prerequisiti
- [Pico SDK](https://github.com/raspberrypi/pico-sdk) installato
- CMake ≥ 3.13
- arm-none-eabi-gcc (toolchain ARM)

### Compilazione
```bash
# Imposta il percorso del SDK (se non già in ~/.bashrc)
export PICO_SDK_PATH=/path/to/pico-sdk

# Entra nella cartella del progetto
cd cdi_nes_controller

# Crea la directory di build e compila
mkdir build && cd build
cmake ..
make -j4
```

Al termine troverai `cdi_nes_controller.uf2` nella cartella `build/`.

### Flash sul Pico
1. Tieni premuto il pulsante **BOOTSEL** sul Pico
2. Collega il Pico al PC via USB
3. Rilascia BOOTSEL → appare il drive `RPI-RP2`
4. Copia `cdi_nes_controller.uf2` sul drive
5. Il Pico si riavvia automaticamente → LED lampeggia 4 volte = OK

---

## Debug

Con il Pico collegato via USB, apri un terminale seriale (115200 baud):

```bash
# Linux/macOS
screen /dev/ttyACM0 115200

# oppure
minicom -b 115200 -D /dev/ttyACM0
```

L'output mostra lo stato grezzo NES e il byte CD-i inviato ad ogni variazione:
```
CDI-NES Controller avviato.
Baud: 1200, TX pin: GPIO0
NES: LATCH=GPIO10 CLOCK=GPIO11 DATA=GPIO12
NES: 0x10  CDI: 0x01   ← tasto SU premuto
NES: 0x00  CDI: 0x00   ← nessun tasto
```

---

## Note sul protocollo CD-i

Il pacchetto inviato è composto da 3 byte:
```
[0x01] [buttons] [~buttons]
```
- `0x01` = header fisso
- `buttons` = bitmask pulsanti attivi
- `~buttons` = checksum (complemento a 1)

> Il protocollo può variare tra modelli di player CD-i (450, 550, 605, ecc.).  
> Se il player non risponde, cattura il segnale di un controller originale  
> con un logic analyzer per confrontare il formato esatto.

---

## File del progetto

| File              | Descrizione                              |
|-------------------|------------------------------------------|
| `CMakeLists.txt`  | Build system CMake                       |
| `cdi_tx.pio`      | State machine PIO per TX seriale CD-i    |
| `main.c`          | Logica principale: lettura NES + invio   |
| `README.md`       | Questo file                              |
