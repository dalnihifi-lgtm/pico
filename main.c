/**
 * cdi_nes_controller — main.c
 *
 * Adattatore NES controller → Philips CD-i
 * Hardware: Raspberry Pi Pico (RP2040)
 *
 * Connessioni:
 *
 *   NES Controller (connettore 7 pin)
 *   ┌──────────────────────────────────────┐
 *   │ Pin 1: +5V  ──────────── VBUS (5V)   │
 *   │ Pin 2: CLOCK ─────────── GPIO 11     │
 *   │ Pin 3: LATCH ─────────── GPIO 10     │
 *   │ Pin 4: DATA  ─────────── GPIO 12     │
 *   │ Pin 7: GND  ──────────── GND         │
 *   └──────────────────────────────────────┘
 *
 *   CD-i Player (DIN 8 pin)
 *   ┌──────────────────────────────────────────────────┐
 *   │ Pin 1: DATA  ── [330Ω] ── GPIO 0                 │
 *   │                        └─ [4.7kΩ] ── +5V (VBUS) │
 *   │ Pin 2: GND   ──────────── GND                    │
 *   │ Pin 3: +5V   ──────────── VBUS (5V)              │
 *   └──────────────────────────────────────────────────┘
 *
 *   LED di stato (opzionale): GPIO 25 (LED onboard del Pico)
 *
 * Mappatura pulsanti NES → CD-i:
 *   NES A       → CD-i Button 1
 *   NES B       → CD-i Button 2
 *   NES SU      → CD-i Su
 *   NES GIÙ     → CD-i Giù
 *   NES SINISTRA→ CD-i Sinistra
 *   NES DESTRA  → CD-i Destra
 *   NES START   → CD-i Button 1 + Button 2 (combo)
 *   NES SELECT  → ignorato (personalizzabile)
 */

#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "cdi_tx.pio.h"

// ---------------------------------------------------------------------------
// Configurazione pin
// ---------------------------------------------------------------------------

#define NES_LATCH_PIN   10
#define NES_CLOCK_PIN   11
#define NES_DATA_PIN    12

#define CDI_TX_PIN      0

#define LED_PIN         25      // LED onboard Pico (PICO_DEFAULT_LED_PIN)

// ---------------------------------------------------------------------------
// Parametri protocollo
// ---------------------------------------------------------------------------

#define CDI_BAUD        1200    // baud rate CD-i

// Timing NES (µs) — basato sulle specifiche originali del 4021
#define NES_LATCH_US    12
#define NES_CLOCK_US    6

// Intervallo polling controller (ms)
#define POLL_INTERVAL_MS  8     // ~125 Hz

// ---------------------------------------------------------------------------
// Bit NES (ordine di uscita dallo shift register 4021, bit 0 = primo)
// ---------------------------------------------------------------------------
#define NES_BIT_A       (1 << 0)
#define NES_BIT_B       (1 << 1)
#define NES_BIT_SELECT  (1 << 2)
#define NES_BIT_START   (1 << 3)
#define NES_BIT_UP      (1 << 4)
#define NES_BIT_DOWN    (1 << 5)
#define NES_BIT_LEFT    (1 << 6)
#define NES_BIT_RIGHT   (1 << 7)

// ---------------------------------------------------------------------------
// Bit CD-i (byte pulsanti nel pacchetto)
// ---------------------------------------------------------------------------
#define CDI_BIT_UP      (1 << 0)
#define CDI_BIT_DOWN    (1 << 1)
#define CDI_BIT_LEFT    (1 << 2)
#define CDI_BIT_RIGHT   (1 << 3)
#define CDI_BIT_BTN1    (1 << 4)
#define CDI_BIT_BTN2    (1 << 5)

// ---------------------------------------------------------------------------
// Variabili globali PIO
// ---------------------------------------------------------------------------
static PIO  g_pio;
static uint g_sm;

// ---------------------------------------------------------------------------
// NES controller
// ---------------------------------------------------------------------------

static void nes_init(void) {
    gpio_init(NES_LATCH_PIN);
    gpio_init(NES_CLOCK_PIN);
    gpio_init(NES_DATA_PIN);

    gpio_set_dir(NES_LATCH_PIN, GPIO_OUT);
    gpio_set_dir(NES_CLOCK_PIN, GPIO_OUT);
    gpio_set_dir(NES_DATA_PIN,  GPIO_IN);

    // Nessun pull necessario: il controller NES ha già resistori interni
    gpio_put(NES_LATCH_PIN, 0);
    gpio_put(NES_CLOCK_PIN, 0);
}

/**
 * Legge lo stato degli 8 pulsanti del controller NES.
 * Restituisce un byte dove 1 = pulsante premuto.
 */
static uint8_t nes_read(void) {
    uint8_t state = 0;

    // Impulso LATCH: campiona lo stato di tutti i pulsanti nel 4021
    gpio_put(NES_LATCH_PIN, 1);
    sleep_us(NES_LATCH_US);
    gpio_put(NES_LATCH_PIN, 0);
    sleep_us(NES_CLOCK_US);

    // Lettura 8 bit: il primo bit (A) è già disponibile su DATA dopo il LATCH
    for (int i = 0; i < 8; i++) {
        // DATA è LOW quando il pulsante è premuto (logica invertita)
        if (!gpio_get(NES_DATA_PIN)) {
            state |= (1 << i);
        }

        // Impulso CLOCK: avanza allo shift register al bit successivo
        gpio_put(NES_CLOCK_PIN, 1);
        sleep_us(NES_CLOCK_US);
        gpio_put(NES_CLOCK_PIN, 0);
        sleep_us(NES_CLOCK_US);
    }

    return state;
}

// ---------------------------------------------------------------------------
// Mappatura NES → CD-i
// ---------------------------------------------------------------------------

static uint8_t nes_to_cdi(uint8_t nes) {
    uint8_t cdi = 0;

    if (nes & NES_BIT_UP)    cdi |= CDI_BIT_UP;
    if (nes & NES_BIT_DOWN)  cdi |= CDI_BIT_DOWN;
    if (nes & NES_BIT_LEFT)  cdi |= CDI_BIT_LEFT;
    if (nes & NES_BIT_RIGHT) cdi |= CDI_BIT_RIGHT;
    if (nes & NES_BIT_A)     cdi |= CDI_BIT_BTN1;
    if (nes & NES_BIT_B)     cdi |= CDI_BIT_BTN2;

    // START → Button 1 + Button 2 simultanei
    if (nes & NES_BIT_START) cdi |= (CDI_BIT_BTN1 | CDI_BIT_BTN2);

    // SELECT → non mappato (aggiungi qui se necessario)

    return cdi;
}

// ---------------------------------------------------------------------------
// CD-i TX
// ---------------------------------------------------------------------------

static void cdi_tx_init(void) {
    g_pio = pio0;
    uint offset = pio_add_program(g_pio, &cdi_tx_program);
    g_sm = pio_claim_unused_sm(g_pio, true);
    cdi_tx_program_init(g_pio, g_sm, offset, CDI_TX_PIN, CDI_BAUD);
}

/**
 * Invia un pacchetto CD-i di 3 byte:
 *   [0x01] [buttons] [~buttons]
 *
 * Nota: il formato esatto può variare tra modelli CD-i.
 * Questo è il formato più comune documentato per i controller
 * della serie "thumbstick" (Remote1/Remote2).
 * Se il tuo player non risponde, prova a catturare il segnale
 * di un controller originale con un logic analyzer.
 */
static void cdi_send_packet(uint8_t buttons) {
    uint8_t packet[3] = {
        0x01,
        buttons,
        (uint8_t)(~buttons)
    };

    for (int i = 0; i < 3; i++) {
        // Attendi che la FIFO TX non sia piena
        pio_sm_put_blocking(g_pio, g_sm, (uint32_t)packet[i]);
        // Pausa tra byte (~1 bit-period extra)
        sleep_us(833);
    }
}

// ---------------------------------------------------------------------------
// LED di stato
// ---------------------------------------------------------------------------

static void led_init(void) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);
}

// Lampeggio rapido all'avvio per confermare il boot
static void led_boot_blink(void) {
    for (int i = 0; i < 4; i++) {
        gpio_put(LED_PIN, 1);
        sleep_ms(80);
        gpio_put(LED_PIN, 0);
        sleep_ms(80);
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
    stdio_init_all();

    led_init();
    led_boot_blink();

    nes_init();
    cdi_tx_init();

    printf("CDI-NES Controller avviato.\n");
    printf("Baud: %d, TX pin: GPIO%d\n", CDI_BAUD, CDI_TX_PIN);
    printf("NES: LATCH=GPIO%d CLOCK=GPIO%d DATA=GPIO%d\n",
           NES_LATCH_PIN, NES_CLOCK_PIN, NES_DATA_PIN);

    uint8_t last_cdi  = 0xFF;   // stato precedente (forza invio al primo ciclo)
    uint8_t last_nes  = 0xFF;

    while (true) {
        uint8_t nes = nes_read();
        uint8_t cdi = nes_to_cdi(nes);

        // Invia solo se lo stato è cambiato (riduce traffico sul bus CD-i)
        if (cdi != last_cdi) {
            cdi_send_packet(cdi);
            last_cdi = cdi;

            // Accendi LED se un pulsante è premuto
            gpio_put(LED_PIN, cdi != 0);

#ifdef PICO_DEFAULT_USB_CDC_STDIO
            // Debug via USB seriale (opzionale, commenta se non serve)
            if (nes != last_nes) {
                printf("NES: 0x%02X  CDI: 0x%02X\n", nes, cdi);
                last_nes = nes;
            }
#endif
        }

        sleep_ms(POLL_INTERVAL_MS);
    }

    return 0;
}
