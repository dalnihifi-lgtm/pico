/**
 * cdi_nes_controller — main.c
 *
 * Adattatore NES controller → Philips CD-i
 * Supporta due modalità di output selezionabili via #define:
 *
 *   MODE_CABLE  — segnale seriale sul cavo (Mini-DIN 8)
 *   MODE_IR     — emulazione telecomando IR (protocollo RC-5 Philips)
 *
 * Hardware: Raspberry Pi Pico (RP2040)
 *
 * ── Connessioni modalità CAVO ────────────────────────────────────────────
 *
 *   NES Controller (7 pin)          RP2040 Pico
 *   Pin 1 +5V  ──────────────────── VBUS
 *   Pin 2 CLOCK ─────────────────── GPIO 11
 *   Pin 3 LATCH ─────────────────── GPIO 10
 *   Pin 4 DATA  ─────────────────── GPIO 12
 *   Pin 7 GND  ──────────────────── GND
 *
 *   CD-i DIN 8                      RP2040 Pico
 *   Pin 1 DATA ── [300Ω] ─────────── GPIO 0
 *                └─ [4.7kΩ] → +5V
 *   Pin 2 GND  ──────────────────── GND
 *   Pin 3 +5V  ──────────────────── VBUS
 *
 * ── Connessioni modalità IR ──────────────────────────────────────────────
 *
 *   NES Controller (7 pin)          RP2040 Pico
 *   (identico a sopra)
 *
 *   LED IR (es. TSAL6200)           RP2040 Pico
 *   Anodo  ── [330Ω] ────────────── GPIO 1
 *   Catodo ──────────────────────── GND
 *
 * ── Protocollo RC-5 (Philips) ────────────────────────────────────────────
 *
 *   14 bit, Manchester encoding, portante 36 kHz
 *   [ S1 | S2 | Toggle | A4 A3 A2 A1 A0 | C5 C4 C3 C2 C1 C0 ]
 *
 *   Indirizzo CD-i: 0x00 (player principale)
 *   Comandi:  Su=0x01  Giù=0x02  Sx=0x04  Dx=0x08
 *             Btn1=0x35 Btn2=0x36 Start=0x0C Stop=0x0D
 */

#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "cdi_tx.pio.h"

// ---------------------------------------------------------------------------
// Seleziona modalità: decommenta UNA sola riga
// ---------------------------------------------------------------------------
//#define MODE_CABLE
#define MODE_IR

// ---------------------------------------------------------------------------
// Pin NES
// ---------------------------------------------------------------------------
#define NES_LATCH_PIN   10
#define NES_CLOCK_PIN   11
#define NES_DATA_PIN    12
#define NES_LATCH_US    12
#define NES_CLOCK_US    6

// ---------------------------------------------------------------------------
// Pin output
// ---------------------------------------------------------------------------
#define CDI_TX_PIN      0       // usato in MODE_CABLE
#define IR_PIN          1       // usato in MODE_IR
#define LED_PIN         25

// ---------------------------------------------------------------------------
// Parametri cavo
// ---------------------------------------------------------------------------
#define CDI_BAUD        1200

// ---------------------------------------------------------------------------
// Parametri RC-5
// ---------------------------------------------------------------------------
#define RC5_CARRIER_HZ  36000
#define RC5_BIT_US      1778    // durata di mezzo bit RC-5 = 889 µs × 2
#define RC5_HALF_US     889     // mezzo periodo di bit
#define RC5_ADDRESS     0x00    // indirizzo CD-i player

// Comandi RC-5 per CD-i
#define RC5_UP          0x01
#define RC5_DOWN        0x02
#define RC5_LEFT        0x04
#define RC5_RIGHT       0x08
#define RC5_BTN1        0x35
#define RC5_BTN2        0x36
#define RC5_START       0x0C

// Polling
#define POLL_INTERVAL_MS  8

// ---------------------------------------------------------------------------
// Bit NES
// ---------------------------------------------------------------------------
#define NES_BIT_A       (1 << 0)
#define NES_BIT_B       (1 << 1)
#define NES_BIT_SELECT  (1 << 2)
#define NES_BIT_START   (1 << 3)
#define NES_BIT_UP      (1 << 4)
#define NES_BIT_DOWN    (1 << 5)
#define NES_BIT_LEFT    (1 << 6)
#define NES_BIT_RIGHT   (1 << 7)

// Bit CD-i (cavo)
#define CDI_BIT_UP      (1 << 0)
#define CDI_BIT_DOWN    (1 << 1)
#define CDI_BIT_LEFT    (1 << 2)
#define CDI_BIT_RIGHT   (1 << 3)
#define CDI_BIT_BTN1    (1 << 4)
#define CDI_BIT_BTN2    (1 << 5)

// ---------------------------------------------------------------------------
// PIO (cavo)
// ---------------------------------------------------------------------------
static PIO  g_pio;
static uint g_sm;

// ---------------------------------------------------------------------------
// NES
// ---------------------------------------------------------------------------
static void nes_init(void) {
    gpio_init(NES_LATCH_PIN);
    gpio_init(NES_CLOCK_PIN);
    gpio_init(NES_DATA_PIN);
    gpio_set_dir(NES_LATCH_PIN, GPIO_OUT);
    gpio_set_dir(NES_CLOCK_PIN, GPIO_OUT);
    gpio_set_dir(NES_DATA_PIN,  GPIO_IN);
    gpio_put(NES_LATCH_PIN, 0);
    gpio_put(NES_CLOCK_PIN, 0);
}

static uint8_t nes_read(void) {
    uint8_t state = 0;
    gpio_put(NES_LATCH_PIN, 1);
    sleep_us(NES_LATCH_US);
    gpio_put(NES_LATCH_PIN, 0);
    sleep_us(NES_CLOCK_US);
    for (int i = 0; i < 8; i++) {
        if (!gpio_get(NES_DATA_PIN)) state |= (1 << i);
        gpio_put(NES_CLOCK_PIN, 1);
        sleep_us(NES_CLOCK_US);
        gpio_put(NES_CLOCK_PIN, 0);
        sleep_us(NES_CLOCK_US);
    }
    return state;
}

// ---------------------------------------------------------------------------
// Modalità CAVO
// ---------------------------------------------------------------------------
#ifdef MODE_CABLE

static void output_init(void) {
    g_pio = pio0;
    uint offset = pio_add_program(g_pio, &cdi_tx_program);
    g_sm = pio_claim_unused_sm(g_pio, true);
    cdi_tx_program_init(g_pio, g_sm, offset, CDI_TX_PIN, CDI_BAUD);
}

static uint8_t nes_to_cdi(uint8_t nes) {
    uint8_t cdi = 0;
    if (nes & NES_BIT_UP)    cdi |= CDI_BIT_UP;
    if (nes & NES_BIT_DOWN)  cdi |= CDI_BIT_DOWN;
    if (nes & NES_BIT_LEFT)  cdi |= CDI_BIT_LEFT;
    if (nes & NES_BIT_RIGHT) cdi |= CDI_BIT_RIGHT;
    if (nes & NES_BIT_A)     cdi |= CDI_BIT_BTN1;
    if (nes & NES_BIT_B)     cdi |= CDI_BIT_BTN2;
    if (nes & NES_BIT_START) cdi |= (CDI_BIT_BTN1 | CDI_BIT_BTN2);
    return cdi;
}

static void send_command(uint8_t nes) {
    uint8_t cdi = nes_to_cdi(nes);
    uint8_t packet[3] = { 0x01, cdi, (uint8_t)(~cdi) };
    for (int i = 0; i < 3; i++) {
        pio_sm_put_blocking(g_pio, g_sm, (uint32_t)packet[i]);
        sleep_us(833);
    }
}

static bool state_changed(uint8_t nes, uint8_t last) {
    return nes_to_cdi(nes) != nes_to_cdi(last);
}

#endif // MODE_CABLE

// ---------------------------------------------------------------------------
// Modalità IR (RC-5)
// ---------------------------------------------------------------------------
#ifdef MODE_IR

static uint8_t g_toggle = 0;   // bit toggle RC-5, si inverte ad ogni pressione

static void output_init(void) {
    gpio_init(IR_PIN);
    gpio_set_dir(IR_PIN, GPIO_OUT);
    gpio_put(IR_PIN, 0);
}

// Emette la portante 36 kHz per 'us' microsecondi
static void ir_carrier_on(uint32_t us) {
    // Periodo portante: 1/36000 ≈ 27.8 µs → HIGH 13 µs, LOW 14 µs
    uint32_t cycles = (us * 36) / 1000;
    for (uint32_t i = 0; i < cycles; i++) {
        gpio_put(IR_PIN, 1);
        sleep_us(9);
        gpio_put(IR_PIN, 0);
        sleep_us(9);
    }
}

static void ir_carrier_off(uint32_t us) {
    gpio_put(IR_PIN, 0);
    sleep_us(us);
}

// Manchester encoding RC-5:
//   bit 1 → metà LOW poi metà HIGH  (transizione ↑ al centro)
//   bit 0 → metà HIGH poi metà LOW  (transizione ↓ al centro)
static void ir_send_bit(uint8_t bit) {
    if (bit) {
        ir_carrier_off(RC5_HALF_US);
        ir_carrier_on(RC5_HALF_US);
    } else {
        ir_carrier_on(RC5_HALF_US);
        ir_carrier_off(RC5_HALF_US);
    }
}

// Invia un comando RC-5 completo
// Formato: S1 S2 Toggle A4..A0 C5..C0
static void ir_send_rc5(uint8_t address, uint8_t command) {
    uint16_t frame = 0;

    // Bit 13-12: start bits (sempre 1,1)
    frame |= (1 << 13);
    frame |= (1 << 12);
    // Bit 11: toggle
    frame |= (g_toggle << 11);
    // Bit 10-6: address (5 bit)
    frame |= ((address & 0x1F) << 6);
    // Bit 5-0: command (6 bit)
    frame |= (command & 0x3F);

    // Trasmetti 14 bit MSB first
    for (int i = 13; i >= 0; i--) {
        ir_send_bit((frame >> i) & 1);
    }

    // Pausa inter-frame (almeno 4 periodi di bit = ~7 ms)
    ir_carrier_off(7000);

    g_toggle ^= 1;  // inverti toggle per il prossimo invio
}

// Mappa NES → comandi RC-5 e li invia tutti (un comando per pulsante premuto)
static void send_command(uint8_t nes) {
    if (nes & NES_BIT_UP)    ir_send_rc5(RC5_ADDRESS, RC5_UP);
    if (nes & NES_BIT_DOWN)  ir_send_rc5(RC5_ADDRESS, RC5_DOWN);
    if (nes & NES_BIT_LEFT)  ir_send_rc5(RC5_ADDRESS, RC5_LEFT);
    if (nes & NES_BIT_RIGHT) ir_send_rc5(RC5_ADDRESS, RC5_RIGHT);
    if (nes & NES_BIT_A)     ir_send_rc5(RC5_ADDRESS, RC5_BTN1);
    if (nes & NES_BIT_B)     ir_send_rc5(RC5_ADDRESS, RC5_BTN2);
    if (nes & NES_BIT_START) ir_send_rc5(RC5_ADDRESS, RC5_START);
}

static bool state_changed(uint8_t nes, uint8_t last) {
    return nes != last;
}

#endif // MODE_IR

// ---------------------------------------------------------------------------
// LED
// ---------------------------------------------------------------------------
static void led_init(void) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);
}

static void led_boot_blink(void) {
    for (int i = 0; i < 4; i++) {
        gpio_put(LED_PIN, 1); sleep_ms(80);
        gpio_put(LED_PIN, 0); sleep_ms(80);
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
    output_init();

#ifdef MODE_CABLE
    printf("Modalita: CAVO (GPIO %d, %d baud)\n", CDI_TX_PIN, CDI_BAUD);
#else
    printf("Modalita: IR RC-5 (GPIO %d, 36 kHz)\n", IR_PIN);
#endif

    uint8_t last_nes = 0xFF;

    while (true) {
        uint8_t nes = nes_read();

        if (state_changed(nes, last_nes)) {
            if (nes != 0) {
                send_command(nes);
                gpio_put(LED_PIN, 1);
            } else {
                gpio_put(LED_PIN, 0);
            }
            last_nes = nes;

            printf("NES: 0x%02X\n", nes);
        }

        sleep_ms(POLL_INTERVAL_MS);
    }

    return 0;
}
