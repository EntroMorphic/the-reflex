/*
 * test_fabric_mul.c — FABRIC MULTIPLY EXPERIMENT (September 2, 2026)
 *
 * THE QUESTION
 *   The production engine streams CPU-premultiplied products on a 2-bit
 *   PARLIO lane (GPIO 4/5) and holds GPIO 6/7 at a static level. The fabric
 *   therefore only population-counts; the CPU performs every ternary multiply
 *   — 2048 tmul() calls per GIE loop, ~880k multiplies/second.
 *
 *   But the PCNT channels are already wired as a ternary multiply:
 *
 *     agree    = (X_POS edge & Y_POS level) | (X_NEG edge & Y_NEG level)  -> +1
 *     disagree = (X_POS edge & Y_NEG level) | (X_NEG edge & Y_POS level)  -> -1
 *     dot      = agree - disagree
 *
 *   If BOTH operands are streamed (4-bit PARLIO, GPIO 4/5 = A, 6/7 = B), the
 *   multiply happens in hardware and the CPU does none of it.
 *
 * ENCODING — one byte per trit product, low nibble data, high nibble zero:
 *
 *     bit0 -> GPIO4 X_POS   A = +1
 *     bit1 -> GPIO5 X_NEG   A = -1
 *     bit2 -> GPIO6 Y_POS   B = +1
 *     bit3 -> GPIO7 Y_NEG   B = -1
 *     bits 4-7 = 0
 *
 *   The zero high-nibble is NOT padding waste. PCNT counts EDGES, so X must
 *   return to zero between products or two consecutive +1 products would
 *   produce only one edge. This is return-to-zero signalling and it is
 *   load-bearing. It is also why the production 2-bit encoding uses 80 bytes
 *   for 160 trits rather than 40.
 *
 * PASS: fabric dot == CPU reference dot, exactly, on every vector.
 */

#include "test_harness.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"

#define FM_N 64          /* trit pairs per vector */

static int8_t fm_a[FM_N];
static int8_t fm_b[FM_N];
static uint8_t fm_buf[FM_N];

/* v1 encoding: both operands in the same nibble.
 * MEASURED DEAD (agree=0/disagree=0 on every vector) while L2.5 proved the
 * 4-bit output path itself is fine. Cause: PARLIO drives all lanes off one
 * clock edge, so Y transitions simultaneously with X. PCNT samples the LEVEL
 * input through GPIO-matrix synchroniser flops, so at X's edge Y still reads
 * its previous value -- always 0 from the RZ nibble. Every channel gates off.
 * Kept for the record; not used. */
static uint8_t fm_encode_v1(int8_t a, int8_t b)
{
    uint8_t sym = 0;
    if (a > 0) sym |= 0x1;
    if (a < 0) sym |= 0x2;
    if (b > 0) sym |= 0x4;
    if (b < 0) sym |= 0x8;
    return sym;
}

/* v2 encoding: present Y ONE NIBBLE EARLY so it is stable at X's edge.
 *   low nibble  (emitted first): Y only, X = 0   -> Y settles, no X edge
 *   high nibble                : Y + X           -> X edge, Y already stable
 * X still returns to 0 at the next pair's low nibble, so RZ is preserved.
 * Still one byte per trit product. */
static uint8_t fm_encode(int8_t a, int8_t b)
{
    uint8_t xb = 0, yb = 0;
    if (a > 0) xb |= 0x1;    /* X_POS  -> GPIO4 */
    if (a < 0) xb |= 0x2;    /* X_NEG  -> GPIO5 */
    if (b > 0) yb |= 0x4;    /* Y_POS  -> GPIO6 */
    if (b < 0) yb |= 0x8;    /* Y_NEG  -> GPIO7 */
    return (uint8_t)(yb | ((yb | xb) << 4));
}

/* CPU reference — the thing we are trying to make unnecessary */
static int fm_cpu_dot(const int8_t *a, const int8_t *b, int n)
{
    int d = 0;
    for (int i = 0; i < n; i++) d += tmul(a[i], b[i]);
    return d;
}

static int fm_run_vector(const char *name, int n)
{
    for (int i = 0; i < n; i++) fm_buf[i] = fm_encode(fm_a[i], fm_b[i]);

    clear_all_pcnt();
    esp_rom_delay_us(50);
    clear_all_pcnt();
    esp_rom_delay_us(50);

    esp_err_t err = gie_parlio_transmit(fm_buf, (size_t)n * 8);
    if (err != ESP_OK) {
        printf("    %-14s TRANSMIT FAILED (%d)\n", name, (int)err);
        return 0;
    }
    esp_rom_delay_us(200);      /* PCNT pipeline settle */

    int agree = 0, disagree = 0;
    gie_read_pcnt(&agree, &disagree);
    int fabric   = agree - disagree;
    int cpu      = fm_cpu_dot(fm_a, fm_b, n);
    int ok       = (fabric == cpu);

    printf("    %-14s fabric=%+4d (agree=%3d disagree=%3d)  cpu=%+4d  %s\n",
           name, fabric, agree, disagree, cpu, ok ? "MATCH" : "*** MISMATCH ***");
    return ok;
}

/* ── Diagnostic ladder ──────────────────────────────────────────────
 * The first run returned agree=0/disagree=0 on every vector: zero counts,
 * not wrong counts. That is a dead signal path, not a wrong multiply, so it
 * says nothing about the hypothesis. Bisect the path before re-testing.
 *
 *   L1: can PCNT count at all?  Drive X_POS from the CPU with Y_POS held
 *       high. Exercises the GPIO->matrix->PCNT input path only.
 *   L2: does the PARLIO *driver* transmit path reach PCNT at 2-bit, i.e.
 *       the production geometry? Isolates "4-bit is wrong" from
 *       "driver transmit is wrong".
 *   L3: the actual 4-bit both-operands test.
 * ────────────────────────────────────────────────────────────────── */
static inline int fm_l1_ok_guard(int l1) { return l1; }

static int fm_l1_cpu_pulse(void)
{
    /* Y_POS high, Y_NEG low => X_POS edges must land in `agree`. */
    esp_rom_gpio_connect_out_signal(4, 128, false, false);   /* X_POS -> plain GPIO */
    esp_rom_gpio_connect_out_signal(6, 128, false, false);   /* Y_POS -> plain GPIO */
    esp_rom_gpio_connect_out_signal(7, 128, false, false);   /* Y_NEG -> plain GPIO */
    gpio_set_level(6, 1);
    gpio_set_level(7, 0);
    gpio_set_level(4, 0);
    esp_rom_delay_us(50);
    clear_all_pcnt(); esp_rom_delay_us(50);
    clear_all_pcnt(); esp_rom_delay_us(50);

    for (int i = 0; i < 20; i++) { gpio_set_level(4, 1); gpio_set_level(4, 0); }
    esp_rom_delay_us(200);

    int a = 0, d = 0;
    gie_read_pcnt(&a, &d);
    int ok = (a == 20 && d == 0);
    printf("    L1 cpu-pulse   agree=%3d disagree=%3d  expect agree=20  %s\n",
           a, d, ok ? "OK" : (a == 0 ? "DEAD PCNT INPUT PATH" : "MISCOUNT"));
    return ok;
}

static int fm_l2_driver_2bit(void)
{
    /* Production geometry: 2-bit PARLIO on X only, Y held static high.
     * One byte = 0x01 -> LSB-first 2-bit groups: 01,00,00,00 => one X_POS
     * pulse with return-to-zero. N bytes => N agree counts. */
    esp_err_t err = gie_reinit_parlio_nbit(2);
    if (err != ESP_OK) { printf("    L2 reinit(2) FAILED %d\n", (int)err); return 0; }
    esp_rom_gpio_connect_out_signal(6, 128, false, false);
    esp_rom_gpio_connect_out_signal(7, 128, false, false);
    gpio_set_level(6, 1);
    gpio_set_level(7, 0);

    for (int i = 0; i < 16; i++) fm_buf[i] = 0x01;
    clear_all_pcnt(); esp_rom_delay_us(50);
    clear_all_pcnt(); esp_rom_delay_us(50);
    err = gie_parlio_transmit(fm_buf, 16 * 8);
    if (err != ESP_OK) { printf("    L2 transmit FAILED %d\n", (int)err); return 0; }
    esp_rom_delay_us(300);

    int a = 0, d = 0;
    gie_read_pcnt(&a, &d);
    int ok = (a == 16 && d == 0);
    printf("    L2 parlio 2bit agree=%3d disagree=%3d  expect agree=16  %s\n",
           a, d, ok ? "OK" : (a == 0 ? "DRIVER TX PATH DEAD" : "MISCOUNT"));
    return ok;
}

/* L2.5: 4-bit PARLIO width, but Y held STATIC high as in production.
 * Separates "4-bit width broke the output path" from "streamed Y does not
 * gate PCNT correctly". Byte 0x01 -> low nibble 0b0001 -> DATA0 (X_POS) only. */
static int fm_l25_4bit_static_y(void)
{
    esp_err_t err = gie_reinit_parlio_nbit(4);
    if (err != ESP_OK) { printf("    L2.5 reinit(4) FAILED %d\n", (int)err); return 0; }
    /* Take GPIO 6,7 back from PARLIO and hold them static, as production does. */
    esp_rom_gpio_connect_out_signal(6, 128, false, false);
    esp_rom_gpio_connect_out_signal(7, 128, false, false);
    gpio_set_level(6, 1);
    gpio_set_level(7, 0);
    esp_rom_delay_us(50);

    for (int i = 0; i < 16; i++) fm_buf[i] = 0x01;
    clear_all_pcnt(); esp_rom_delay_us(50);
    clear_all_pcnt(); esp_rom_delay_us(50);
    err = gie_parlio_transmit(fm_buf, 16 * 8);
    if (err != ESP_OK) { printf("    L2.5 transmit FAILED %d\n", (int)err); return 0; }
    esp_rom_delay_us(300);

    int a = 0, d = 0;
    gie_read_pcnt(&a, &d);
    /* 16 bytes = 32 nibbles; every other nibble is 0x1 => 16 X_POS pulses. */
    int ok = (a > 0);
    printf("    L2.5 4bit,Y=1  agree=%3d disagree=%3d  expect agree>0    %s\n",
           a, d, ok ? "OK — 4-bit output path is fine"
                    : "DEAD — 4-bit width itself broke the output");
    return ok;
}

int run_test_fabric_mul(void)
{
    printf("-- FABRIC MULTIPLY: stream both operands, CPU does zero multiplies --\n");
    fflush(stdout);

    printf("  Diagnostic ladder (previous run: agree=0/disagree=0 everywhere):\n");
    int l1 = fm_l1_cpu_pulse();
    int l2 = fm_l2_driver_2bit();
    int l25 = fm_l1_ok_guard(l1) ? fm_l25_4bit_static_y() : 0;
    printf("\n");
    fflush(stdout);
    if (!l1) {
        printf("  L1 failed: PCNT cannot count even a CPU-driven edge.\n");
        printf("  The fault is the PCNT input path, not the 4-bit hypothesis.\n");
        printf("  FABRIC MULTIPLY: INCONCLUSIVE\n\n");
        return 0;
    }
    if (l2 && !l25) {
        printf("  L1+L2 OK, L2.5 dead: 4-bit PARLIO width itself breaks the\n");
        printf("  output path, independent of whether Y is streamed.\n");
        printf("  FABRIC MULTIPLY: INCONCLUSIVE (4-bit output path)\n\n");
        return 0;
    }
    if (!l2) {
        printf("  L1 OK but L2 failed: PCNT works, the PARLIO *driver* transmit\n");
        printf("  path does not reach it. The production engine drives PARLIO\n");
        printf("  bare-metal (tx_bytelen + tx_start + GDMA), not via the driver\n");
        printf("  API. The 4-bit hypothesis is UNTESTED, not disproved.\n");
        printf("  FABRIC MULTIPLY: INCONCLUSIVE\n\n");
        return 0;
    }

    printf("  Reconfiguring PARLIO 2-bit -> 4-bit (GPIO 4,5 = A | GPIO 6,7 = B)...\n");
    esp_err_t err = gie_reinit_parlio_nbit(4);
    if (err != ESP_OK) {
        printf("  FAILED to reconfigure PARLIO: %d\n", (int)err);
        printf("  FABRIC MULTIPLY: FAIL\n\n");
        return 0;
    }
    printf("  PARLIO 4-bit OK. PCNT wiring unchanged (already a multiply table).\n\n");
    fflush(stdout);

    int pass = 0, total = 0;

    /* 1. all (+1,+1) -> every product +1 */
    for (int i = 0; i < FM_N; i++) { fm_a[i] = T_POS; fm_b[i] = T_POS; }
    total++; pass += fm_run_vector("all +1*+1", FM_N);

    /* 2. all (-1,-1) -> every product +1 */
    for (int i = 0; i < FM_N; i++) { fm_a[i] = T_NEG; fm_b[i] = T_NEG; }
    total++; pass += fm_run_vector("all -1*-1", FM_N);

    /* 3. all (+1,-1) -> every product -1 */
    for (int i = 0; i < FM_N; i++) { fm_a[i] = T_POS; fm_b[i] = T_NEG; }
    total++; pass += fm_run_vector("all +1*-1", FM_N);

    /* 4. zeros -> no edges at all, dot must be 0 */
    for (int i = 0; i < FM_N; i++) { fm_a[i] = T_ZERO; fm_b[i] = T_ZERO; }
    total++; pass += fm_run_vector("all zeros", FM_N);

    /* 5. alternating +1/-1 products -> exact cancellation */
    for (int i = 0; i < FM_N; i++) { fm_a[i] = T_POS; fm_b[i] = (i & 1) ? T_NEG : T_POS; }
    total++; pass += fm_run_vector("alternating", FM_N);

    /* 6-10. random ternary vectors — the real test */
    cfc_seed(0xFAB21C05);
    for (int v = 0; v < 5; v++) {
        for (int i = 0; i < FM_N; i++) { fm_a[i] = rand_trit(30); fm_b[i] = rand_trit(30); }
        char nm[16];
        snprintf(nm, sizeof(nm), "random %d", v + 1);
        total++; pass += fm_run_vector(nm, FM_N);
    }

    printf("\n  RESULT: %d / %d vectors matched the CPU reference exactly\n", pass, total);
    if (pass == total) {
        printf("  The ternary MULTIPLY ran in the peripheral fabric.\n");
        printf("  CPU tmul() calls during the dot product: ZERO.\n");
        printf("  FABRIC MULTIPLY: PASS\n\n");
    } else {
        printf("  FABRIC MULTIPLY: FAIL\n\n");
    }
    fflush(stdout);
    return (pass == total);
}
