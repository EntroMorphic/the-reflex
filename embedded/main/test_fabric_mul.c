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

#define FM_N 64          /* trit pairs per vector */

static int8_t fm_a[FM_N];
static int8_t fm_b[FM_N];
static uint8_t fm_buf[FM_N];

/* Encode one trit pair into a byte: low nibble = product operands, high = RZ */
static uint8_t fm_encode(int8_t a, int8_t b)
{
    uint8_t sym = 0;
    if (a > 0) sym |= 0x1;   /* X_POS */
    if (a < 0) sym |= 0x2;   /* X_NEG */
    if (b > 0) sym |= 0x4;   /* Y_POS */
    if (b < 0) sym |= 0x8;   /* Y_NEG */
    return sym;              /* high nibble stays 0 => return-to-zero */
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

int run_test_fabric_mul(void)
{
    printf("-- FABRIC MULTIPLY: stream both operands, CPU does zero multiplies --\n");
    fflush(stdout);

    printf("  Reconfiguring PARLIO 2-bit -> 4-bit (GPIO 4,5 = A | GPIO 6,7 = B)...\n");
    esp_err_t err = gie_reinit_parlio_4bit();
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
