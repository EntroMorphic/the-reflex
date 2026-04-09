/*
 * test_live_input.c — Test 11 (Pattern Classification + Enrollment)
 *
 * Split from geometry_cfc_freerun.c: April 8, 2026 (audit remediation).
 * Test 11 is the system's enrollment and classification test: it observes
 * Board B's 4-pattern cycling, builds ternary signatures, enrolls them
 * as GIE W_f weights, and verifies both CPU and ISR classification.
 * It also builds the 7-voxel TriX cube and runs streaming classification
 * with online maintenance. Populates sig[] for Tests 12-14C.
 *
 * Board: ESP32-C6FH4 (QFN32) rev v0.2
 * ESP-IDF: v5.4
 */

#include "test_harness.h"

int run_test_11(void) {
    /* ══════════════════════════════════════════════════════════════
     *  TEST 11: Pattern Classification — Stream-Based Continuous CfC
     *
     *  THE TASK: Board B cycles through 4 patterns. Board A runs the
     *  GIE CONTINUOUSLY with live ESP-NOW input and classifies which
     *  pattern is active from the 32-trit hidden state.
     *
     *  v3 CHANGE: Stream processing instead of polling.
     *  - Ring buffer captures EVERY packet with real arrival timestamp
     *  - espnow_drain() retrieves all packets since last call
     *  - Each packet immediately drives GIE input update
     *  - Inter-packet timing uses real RF arrival times, not poll intervals
     *  - Baseline classifier uses the same real timing data
     *
     *  Protocol:
     *  1. WARMUP: Start GIE, drain+process all packets for 10s.
     *  2. TRAINING: Continue (no reset). 500ms windows. Store template
     *     for each pattern when window is pure.
     *  3. TESTING: Continue (no reset). 500ms windows. Classify by
     *     nearest template. Score against ground truth.
     *  4. BASELINE: Packet-rate classifier using real inter-packet gaps.
     *     P0(10Hz)=~100ms, P1(burst)=~50ms, P2(2Hz)=~500ms, P3(10Hz)=~100ms.
     *     Baseline CANNOT distinguish P0/P3 (same rate, different payload).
     *
     *  The GIE runs as ONE CONTINUOUS SESSION from warmup through test.
     * ══════════════════════════════════════════════════════════════ */
    printf("-- TEST 11: Pattern Classification (Stream CfC) --\n");
    fflush(stdout);
    {   /* scope block for Test 11 locals */
        #define T11_WINDOW_MS      1000   /* Sample window duration (1s) */
        /* NUM_TEMPLATES defined at file scope */
        #define MAX_TEST_SAMPLES   32     /* Test samples to collect */

        /* ── TriX Cube: 7-voxel geometry (core + 6 faces) ──
         * Each voxel holds 4 pattern signatures in input space (128 trits).
         * The core observes raw input. Each face observes input under a
         * different temporal filter condition. Ensemble vote for classification. */
        #define TVOX_K         4     /* patterns per voxel */
        #define TVOX_FACES     6
        #define TVOX_TOTAL     7     /* [0]=core, [1..6]=faces */

        /* Face filter IDs */
        #define FACE_RECENT    1     /* +x: input from last 1s */
        #define FACE_PRIOR     2     /* -x: input from 1-3s ago */
        #define FACE_STABLE    3     /* +y: input during long dwell (>2s same pattern) */
        #define FACE_TRANSIENT 4     /* -y: input within 500ms of pattern change */
        #define FACE_CONFIDENT 5     /* +z: input when core score > 100 */
        #define FACE_UNCERTAIN 6     /* -z: input when core score < 80 */

        typedef struct {
            int8_t  sig[TVOX_K][CFC_INPUT_DIM];
            int16_t sig_sum[TVOX_K][CFC_INPUT_DIM];
            int     sig_count[TVOX_K];
            int     total_samples;
        } trix_voxel_t;

        static trix_voxel_t cube[TVOX_TOTAL];
        /* Memory: 7 × (4×128 + 4×256 + 4×4 + 4) = 7 × 1556 = 10,892 bytes */

        /* Drain buffer — big enough for one drain cycle at max rate (static for stack) */

        /* Start GIE — ONE init for the entire test */
        cfc_init(42, 50);
        premultiply_all();
        encode_all_neurons();
        build_circular_chain();
        start_freerun();
        espnow_last_rx_us = 0;
        gie_reset_gap_history();
        espnow_ring_flush();  /* Discard any stale packets */

        /* ── Phase 0: TriX Signature Routing ──
         * "Don't learn what you can read." — TriX principle
         *
         * Instead of training weights, compute the MEAN TERNARY SIGNATURE
         * of each pattern's INPUT during observation, then classify by
         * dot product: argmax(input @ signatures.T).
         *
         * The 128-trit input already contains pattern ID (trits 16..23),
         * payload (24..87), and timing (88..103). The signature captures
         * ALL of this. Content-addressable routing, not learning. */

        /* Signature storage: file-scope for access by diagnostics */
        static int16_t sig_sum[NUM_TEMPLATES][CFC_INPUT_DIM];
        int sig_count[NUM_TEMPLATES];
        memset(sig_sum, 0, sizeof(sig_sum));
        memset(sig_count, 0, sizeof(sig_count));

        /* ── Phase 0a: Observe inputs per pattern (30s) ── */
        printf("  Phase 0a: Observing input signatures (30s)...\n");
        fflush(stdout);

        int obs_packets = 0;
        int obs_updates = 0;
        int obs_cur_pattern = -1;
        int64_t obs_start = esp_timer_get_time();

        while ((esp_timer_get_time() - obs_start) < 30000000LL) {  /* 30s */
            vTaskDelay(pdMS_TO_TICKS(T11_DRAIN_MS));
            int n = espnow_drain(drain_buf, 32);
            for (int i = 0; i < n; i++) {
                obs_packets++;
                if (drain_buf[i].pkt.pattern_id < 4)
                    obs_cur_pattern = drain_buf[i].pkt.pattern_id;

                if (espnow_encode_rx_entry(&drain_buf[i], NULL)) {
                    update_gie_input();
                    obs_updates++;

                    /* Accumulate this input into the current pattern's sum */
                    if (obs_cur_pattern >= 0 && obs_cur_pattern < 4) {
                        for (int j = 0; j < CFC_INPUT_DIM; j++) {
                            sig_sum[obs_cur_pattern][j] += cfc.input[j];
                        }
                        sig_count[obs_cur_pattern]++;
                    }
                }
            }
        }

        printf("  Observed: %d packets, %d encoded, %d GIE loops\n",
               obs_packets, obs_updates, (int)loop_count);
        printf("  Per-pattern counts: P0=%d P1=%d P2=%d P3=%d\n",
               sig_count[0], sig_count[1], sig_count[2], sig_count[3]);
        fflush(stdout);

        /* ── Phase 0b: Compute signatures — sign(sig_sum) ── */
        printf("  Phase 0b: Computing TriX signatures...\n");
        fflush(stdout);

        for (int p = 0; p < NUM_TEMPLATES; p++) {
            int nz = 0;
            for (int i = 0; i < CFC_INPUT_DIM; i++) {
                sig[p][i] = tsign(sig_sum[p][i]);
                if (sig[p][i] != T_ZERO) nz++;
            }
            /* Zero out sequence features [104..127] in the signature.
             *
             * The 128-trit input encodes:
             *   [0..15]    RSSI — shared across patterns (same sender)
             *              but harmless (contributes equally to all dots)
             *   [16..23]   Pattern ID one-hot — primary discriminator
             *   [24..87]   Payload bits — pattern-specific content
             *   [88..103]  Inter-packet timing — pattern-discriminative
             *              (P0=100ms, P1=burst+pause, P2=500ms)
             *   [104..119] Sequence features — global monotonic counter,
             *              NOT pattern-specific. Bakes observation-window
             *              sequence range into signature, producing noise
             *              at test time when sequences are out of range.
             *   [120..127] Reserved — always zero
             *
             * Only [104..127] are masked. RSSI is harmless (shift-
             * invariant under argmax). Timing IS discriminative (different
             * patterns have different inter-packet gaps). */
            for (int i = 104; i < CFC_INPUT_DIM; i++)
                sig[p][i] = T_ZERO;     /* sequence + reserved */
            /* Recount non-zero after masking */
            nz = 0;
            for (int i = 0; i < CFC_INPUT_DIM; i++)
                if (sig[p][i] != T_ZERO) nz++;
            printf("  sig[%d]: [", p);
            /* Print first 32 trits for visibility */
            for (int i = 0; i < 32 && i < CFC_INPUT_DIM; i++)
                printf("%c", trit_char(sig[p][i]));
            printf("...] nz=%d/%d (from %d samples, seq trits masked)\n",
                   nz, CFC_INPUT_DIM, sig_count[p]);
        }

        /* Print pattern ID region (trits 16..23) for each signature */
        printf("  Signature pattern-ID trits [16..23]:\n");
        for (int p = 0; p < NUM_TEMPLATES; p++) {
            printf("    sig[%d][16..23]: ", p);
            for (int i = 16; i < 24; i++)
                printf("%c", trit_char(sig[p][i]));
            printf("\n");
        }

        /* Cross-dot: dot(sig[i], sig[j]) for all pairs */
        printf("  Signature cross-dots:\n");
        for (int i = 0; i < NUM_TEMPLATES; i++) {
            printf("    sig[%d] vs: ", i);
            for (int j = 0; j < NUM_TEMPLATES; j++) {
                int d = 0;
                for (int k = 0; k < CFC_INPUT_DIM; k++) {
                    if (sig[i][k] != T_ZERO && sig[j][k] != T_ZERO)
                        d += tmul(sig[i][k], sig[j][k]);
                }
                printf("s%d=%d ", j, d);
            }
            printf("\n");
        }
        printf("  Ring drops: %d\n", (int)espnow_ring_drops());
        fflush(stdout);

        /* ── Phase 0c: Install signatures as W_f weights ──
         * TriX insight: signatures ARE the optimal gate weights.
         * Neuron n assigned to pattern (n/8). W_f input portion = sig[p].
         * W_f hidden portion zeroed (gate depends only on input match).
         * W_g left random (candidate value is pattern-agnostic). */
        printf("\n  Phase 0c: Installing signatures as W_f gate weights...\n");
        fflush(stdout);

        for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
            int assigned = n / 8;  /* 8 neurons per pattern */
            for (int i = 0; i < CFC_INPUT_DIM; i++)
                cfc.W_f[n][i] = sig[assigned][i];
            for (int i = CFC_INPUT_DIM; i < CFC_CONCAT_DIM; i++)
                cfc.W_f[n][i] = T_ZERO;
        }

        /* Phase 3: Disable CfC blend entirely — all neurons HOLD.
         * gate_threshold = INT32_MAX means no f_dot ever exceeds threshold,
         * so every neuron takes f=T_ZERO → h_new[n] = h_old[n].
         * Hidden state freezes after input install; classification is TriX-only.
         * Previous value: 90 (selective gating, ~10% fire rate).
         * Reversible: set back to 90 to re-enable CfC blend. */
        gate_threshold = 0x7FFFFFFF;  /* INT32_MAX — no neuron fires */
        gate_fires_total = 0;
        gate_steps_total = 0;

        /* Re-premultiply and re-encode with new W_f weights */
        premultiply_all();
        encode_all_neurons();

        /* Enable TriX ISR classification — the ISR now extracts per-pattern
         * scores from the f-pathway dots and publishes trix_pred at 430 Hz. */
        trix_enabled = 1;

        printf("  W_f weights set from signatures (8 neurons/pattern)\n");
        printf("  Gate threshold: %d (Phase 3: blend DISABLED, all neurons HOLD)\n",
               (int)gate_threshold);

        /* Print assignment and expected dots */
        for (int n = 0; n < CFC_HIDDEN_DIM; n++) {
            int assigned = n / 8;
            if (n % 8 == 0) {
                printf("  Neurons %d-%d → pattern %d\n", n, n + 7, assigned);
            }
        }
        fflush(stdout);

        /* Gate selectivity diagnostic */
        int avg_fire_pct = (gate_steps_total > 0)
            ? (int)(gate_fires_total * 100LL / gate_steps_total) : 0;
        printf("  Gate selectivity: %d fires / %d steps = %d%%\n",
               (int)gate_fires_total, (int)gate_steps_total, avg_fire_pct);
        fflush(stdout);

        /* ── GDMA offset calibration ──
         * The GDMA circular chain offset means ISR group index g may not
         * correspond to pattern g. Calibrate by matching ISR group scores
         * (from a clean trix_channel) against CPU-computed pattern dots.
         * Build the permutation trix_group_to_pattern[4].
         *
         * Wait for a live packet, encode it, wait for a clean ISR loop,
         * then match all 4 groups to all 4 patterns by closest dot. */
        {
            printf("\n  GDMA offset calibration...\n");
            fflush(stdout);

            /* Wait for a live packet */
            int cal_got_packet = 0;
            int64_t cal_start = esp_timer_get_time();
            while (!cal_got_packet && (esp_timer_get_time() - cal_start) < 5000000LL) {
                vTaskDelay(pdMS_TO_TICKS(10));
                int nd = espnow_drain(drain_buf, 32);
                for (int i = 0; i < nd && !cal_got_packet; i++) {
                    if (espnow_encode_rx_entry(&drain_buf[i], NULL)) {
                        gie_input_pending = 1;
                        int spins = 0;
                        while (gie_input_pending && spins < 5000) {
                            esp_rom_delay_us(5);
                            spins++;
                        }
                        cal_got_packet = 1;
                    }
                }
            }

            if (cal_got_packet) {
                /* Wait for a clean ISR classification */
                uint32_t seq_before = trix_channel.sequence;
                uint32_t new_seq = reflex_wait_timeout(
                    &trix_channel, seq_before, 16000000);

                if (new_seq != 0) {
                    /* Unpack ISR group scores */
                    uint32_t packed = reflex_read(&trix_channel);
                    int32_t isr_g[4];
                    for (int g = 0; g < 4; g++)
                        isr_g[g] = (int8_t)((packed >> (g * 8)) & 0xFF);

                    /* Compute CPU pattern dots */
                    int cpu_d[4] = {0};
                    for (int p = 0; p < 4; p++)
                        for (int j = 0; j < CFC_INPUT_DIM; j++)
                            if (sig[p][j] != T_ZERO && cfc.input[j] != T_ZERO)
                                cpu_d[p] += tmul(sig[p][j], cfc.input[j]);

                    /* Greedy assignment: for each ISR group, find the CPU
                     * pattern with the closest dot product. Mark used. */
                    int8_t map[4] = {-1, -1, -1, -1};
                    int used[4] = {0, 0, 0, 0};
                    for (int g = 0; g < 4; g++) {
                        int best_p = -1, best_dist = 9999;
                        for (int p = 0; p < 4; p++) {
                            if (used[p]) continue;
                            int dist = isr_g[g] - cpu_d[p];
                            if (dist < 0) dist = -dist;
                            if (dist < best_dist) {
                                best_dist = dist;
                                best_p = p;
                            }
                        }
                        if (best_p >= 0) {
                            map[g] = (int8_t)best_p;
                            used[best_p] = 1;
                        }
                    }

                    /* Install the mapping */
                    for (int g = 0; g < 4; g++)
                        trix_group_to_pattern[g] = map[g];

                    printf("  GDMA offset mapping: G0→P%d G1→P%d G2→P%d G3→P%d\n",
                           map[0], map[1], map[2], map[3]);
                    printf("  ISR groups:  [%d, %d, %d, %d]\n",
                           (int)isr_g[0], (int)isr_g[1], (int)isr_g[2], (int)isr_g[3]);
                    printf("  CPU patterns: [%d, %d, %d, %d]\n",
                           cpu_d[0], cpu_d[1], cpu_d[2], cpu_d[3]);
                } else {
                    printf("  WARN: no clean ISR loop — using identity mapping\n");
                }
            } else {
                printf("  WARN: no packet in 5s — using identity mapping\n");
            }
            fflush(stdout);
        }

        /* ── Phase 0d: TriX Cube observation (15s) ──
         * Initialize 7-voxel cube. Core (cube[0]) uses the already-computed
         * signatures. 6 face voxels observe the input under temporal filters.
         * All voxels share the 128-trit input space. */
        printf("\n  Phase 0d: TriX Cube face observation (15s)...\n");
        fflush(stdout);

        /* Initialize cube — core copies from main signatures */
        memset(cube, 0, sizeof(cube));
        memcpy(cube[0].sig, sig, sizeof(sig));
        /* Core sig_sum/sig_count already tracked in the main arrays */

        /* Face observation state */
        int face_obs_cur_pattern = -1;
        int face_obs_prev_pattern = -1;
        int64_t face_pattern_start_us = 0;    /* when current pattern started */
        int64_t face_last_change_us = 0;      /* when last pattern change happened */
        int face_obs_pkts = 0;
        int64_t face_obs_start = esp_timer_get_time();

        while ((esp_timer_get_time() - face_obs_start) < 15000000LL) {  /* 15s */
            vTaskDelay(pdMS_TO_TICKS(T11_DRAIN_MS));
            int n = espnow_drain(drain_buf, 32);
            for (int i = 0; i < n; i++) {
                face_obs_pkts++;
                if (drain_buf[i].pkt.pattern_id < 4) {
                    if (face_obs_cur_pattern != (int)drain_buf[i].pkt.pattern_id) {
                        face_obs_prev_pattern = face_obs_cur_pattern;
                        face_obs_cur_pattern = drain_buf[i].pkt.pattern_id;
                        face_last_change_us = esp_timer_get_time();
                        face_pattern_start_us = face_last_change_us;
                    }
                }

                if (!espnow_encode_rx_entry(&drain_buf[i], NULL)) continue;
                update_gie_input();
                if (face_obs_cur_pattern < 0 || face_obs_cur_pattern >= 4) continue;

                int p = face_obs_cur_pattern;
                int64_t now_us = esp_timer_get_time();
                int64_t dwell_us = now_us - face_pattern_start_us;
                int64_t since_change_us = now_us - face_last_change_us;

                /* Core dot for confidence filtering */
                int core_score = 0;
                for (int j = 0; j < CFC_INPUT_DIM; j++) {
                    if (sig[p][j] != T_ZERO && cfc.input[j] != T_ZERO)
                        core_score += tmul(sig[p][j], cfc.input[j]);
                }

                /* Face 1 (+x): Recent — all packets in this phase */
                for (int j = 0; j < CFC_INPUT_DIM; j++)
                    cube[FACE_RECENT].sig_sum[p][j] += cfc.input[j];
                cube[FACE_RECENT].sig_count[p]++;
                cube[FACE_RECENT].total_samples++;

                /* Face 2 (-x): Prior — packets where prev pattern is known
                 * (accumulates under the PREVIOUS pattern's label) */
                if (face_obs_prev_pattern >= 0 && face_obs_prev_pattern < 4) {
                    for (int j = 0; j < CFC_INPUT_DIM; j++)
                        cube[FACE_PRIOR].sig_sum[face_obs_prev_pattern][j] += cfc.input[j];
                    cube[FACE_PRIOR].sig_count[face_obs_prev_pattern]++;
                    cube[FACE_PRIOR].total_samples++;
                }

                /* Face 3 (+y): Stable — dwell > 2s */
                if (dwell_us > 2000000LL) {
                    for (int j = 0; j < CFC_INPUT_DIM; j++)
                        cube[FACE_STABLE].sig_sum[p][j] += cfc.input[j];
                    cube[FACE_STABLE].sig_count[p]++;
                    cube[FACE_STABLE].total_samples++;
                }

                /* Face 4 (-y): Transient — within 500ms of pattern change */
                if (since_change_us < 500000LL) {
                    for (int j = 0; j < CFC_INPUT_DIM; j++)
                        cube[FACE_TRANSIENT].sig_sum[p][j] += cfc.input[j];
                    cube[FACE_TRANSIENT].sig_count[p]++;
                    cube[FACE_TRANSIENT].total_samples++;
                }

                /* Face 5 (+z): Confident — core match > 100 */
                if (core_score > 100) {
                    for (int j = 0; j < CFC_INPUT_DIM; j++)
                        cube[FACE_CONFIDENT].sig_sum[p][j] += cfc.input[j];
                    cube[FACE_CONFIDENT].sig_count[p]++;
                    cube[FACE_CONFIDENT].total_samples++;
                }

                /* Face 6 (-z): Uncertain — core match < 80 */
                if (core_score < 80) {
                    for (int j = 0; j < CFC_INPUT_DIM; j++)
                        cube[FACE_UNCERTAIN].sig_sum[p][j] += cfc.input[j];
                    cube[FACE_UNCERTAIN].sig_count[p]++;
                    cube[FACE_UNCERTAIN].total_samples++;
                }
            }
        }

        /* Compute face signatures */
        const char *face_names[] = {"core", "+x:recent", "-x:prior",
                                    "+y:stable", "-y:transient",
                                    "+z:confident", "-z:uncertain"};
        printf("  TriX Cube face signatures:\n");
        for (int v = 1; v < TVOX_TOTAL; v++) {
            for (int p = 0; p < TVOX_K; p++) {
                int nz = 0;
                for (int j = 0; j < CFC_INPUT_DIM; j++) {
                    cube[v].sig[p][j] = tsign(cube[v].sig_sum[p][j]);
                    if (cube[v].sig[p][j] != T_ZERO) nz++;
                }
            }
            /* Print summary: total samples and per-pattern counts */
            printf("    %s: %d samples [P0=%d P1=%d P2=%d P3=%d]\n",
                   face_names[v], cube[v].total_samples,
                   cube[v].sig_count[0], cube[v].sig_count[1],
                   cube[v].sig_count[2], cube[v].sig_count[3]);
        }

        /* Face divergence from core: Hamming(face_sig[p], core_sig[p]) */
        printf("  Face divergence from core (avg Hamming across patterns):\n");
        for (int v = 1; v < TVOX_TOTAL; v++) {
            int total_hamm = 0, counted = 0;
            for (int p = 0; p < TVOX_K; p++) {
                if (cube[v].sig_count[p] > 0) {
                    total_hamm += trit_hamming(cube[0].sig[p],
                                              cube[v].sig[p], CFC_INPUT_DIM);
                    counted++;
                }
            }
            printf("    %s: %d avg (%d patterns with data)\n",
                   face_names[v],
                   counted > 0 ? total_hamm / counted : 0, counted);
        }
        printf("  Ring drops: %d\n", (int)espnow_ring_drops());
        fflush(stdout);

        /* Save original core signatures for drift measurement */
        static int8_t sig_orig[NUM_TEMPLATES][CFC_INPUT_DIM];
        memcpy(sig_orig, sig, sizeof(sig));

        #define RESIGN_INTERVAL   16   /* re-sign every 16 packets per pattern */

        {
            /* ── Phase 1: TriX Cube ensemble classification (60s) ──
             * Core + 6 faces each vote per-packet. Ensemble = majority.
             * Online maintenance on core. Novelty detection on core. */
            printf("\n  Phase 1: TriX Cube ensemble (up to %d samples, 60s)...\n",
                   MAX_TEST_SAMPLES);
            fflush(stdout);

            int core_correct = 0, sig_total = 0;
            int isr_correct = 0;  /* ISR-level TriX classification (window-level) */
            int isr_attempted = 0; /* windows where ISR had at least one clean vote */
            int isr_pkt_clean = 0; /* packets where ISR clean-loop was obtained */
            int isr_pkt_timeout = 0; /* packets where ISR clean-loop wait timed out */
            int baseline_correct = 0;
            int total_novel = 0, total_classified = 0;
            int total_resigns = 0;
            int novel_windows = 0;
            /* Global XOR mask accumulators (across all windows) */
            int g_xor_total[TVOX_TOTAL] = {0};
            int g_xor_rssi[TVOX_TOTAL] = {0};
            int g_xor_patid[TVOX_TOTAL] = {0};
            int g_xor_payload[TVOX_TOTAL] = {0};
            int g_xor_timing[TVOX_TOTAL] = {0};
            int g_xor_count[TVOX_TOTAL] = {0};
            int64_t test_start = esp_timer_get_time();
            int64_t test_timeout_us = 60LL * 1000000LL;

            while (sig_total < MAX_TEST_SAMPLES &&
                   (esp_timer_get_time() - test_start) < test_timeout_us) {

                /* Per-window accumulators */
                int test_votes[4] = {0};
                int core_votes[4] = {0};
                int isr_votes[4] = {0};
                int test_packets = 0;
                int novel_in_window = 0;
                int64_t gap_sum_ms = 0;
                int gap_count = 0;
                int64_t prev_pkt_us = 0;
                /* Per-window XOR mask accumulators per face */
                int xor_mask_total[TVOX_TOTAL] = {0};
                int xor_mask_rssi[TVOX_TOTAL] = {0};
                int xor_mask_patid[TVOX_TOTAL] = {0};
                int xor_mask_payload[TVOX_TOTAL] = {0};
                int xor_mask_timing[TVOX_TOTAL] = {0};
                int xor_mask_count[TVOX_TOTAL] = {0};
                int64_t window_start = esp_timer_get_time();

                while ((esp_timer_get_time() - window_start) < (T11_WINDOW_MS * 1000LL)) {
                    vTaskDelay(pdMS_TO_TICKS(T11_DRAIN_MS));
                    int n = espnow_drain(drain_buf, 32);
                    for (int i = 0; i < n; i++) {
                        int64_t pkt_gap_ms = 0;
                        if (espnow_encode_rx_entry(&drain_buf[i], &pkt_gap_ms)) {
                            /* Don't call update_gie_input() — that races the DMA.
                             * Instead, set the flag so the ISR re-encodes during
                             * the next loop boundary (when PARLIO is stopped). */
                            gie_input_pending = 1;

                            /* Wait for ISR to re-encode (clears gie_input_pending) */
                            {
                                int spins = 0;
                                while (gie_input_pending && spins < 5000) {
                                    esp_rom_delay_us(5);
                                    spins++;
                                }
                                /* Wait for a CLEAN classification via trix_channel.
                                 * The ISR signals the channel on every clean loop with
                                 * the 4 group dots packed into the channel value.
                                 * We snapshot the sequence before the encode, then wait
                                 * for it to advance (meaning a clean loop occurred with
                                 * the new input data).
                                 * Timeout: 100ms = 16,000,000 cycles at 160 MHz. */
                                uint32_t seq_before = trix_channel.sequence;
                                uint32_t new_seq = reflex_wait_timeout(
                                    &trix_channel, seq_before, 16000000);
                                int got_clean = (new_seq != 0);

                                /* Resolve ISR classification — but ONLY if we got
                                 * a fresh clean loop. If timed out, no clean data. */
                                int isr_p = -1;
                                int cpu_d[4] = {0};
                                if (got_clean) {
                                    isr_pkt_clean++;
                                    /* Unpack 4 group dots from channel value.
                                     * Packed as signed bytes: d0|d1<<8|d2<<16|d3<<24 */
                                    uint32_t packed = reflex_read(&trix_channel);
                                    int32_t isr_ts[4];
                                    for (int g = 0; g < 4; g++)
                                        isr_ts[g] = (int8_t)((packed >> (g * 8)) & 0xFF);

                                    /* Compute CPU reference dots from sig[] */
                                    for (int p = 0; p < 4; p++) {
                                        for (int j = 0; j < CFC_INPUT_DIM; j++) {
                                            if (sig[p][j] != T_ZERO && cfc.input[j] != T_ZERO)
                                                cpu_d[p] += tmul(sig[p][j], cfc.input[j]);
                                        }
                                    }
                                    /* Find the ISR argmax group */
                                    int isr_best = -9999;
                                    for (int g = 0; g < 4; g++) {
                                        if (isr_ts[g] > isr_best)
                                            isr_best = isr_ts[g];
                                    }
                                    /* Match ISR's max value to the CPU pattern with
                                     * the closest dot. sig[] and W_f[] are always in
                                     * sync (both updated atomically at resign), so the
                                     * ISR max should exactly equal the CPU max for the
                                     * correct pattern. */
                                    int best_dist = 9999;
                                    for (int p = 0; p < 4; p++) {
                                        int dist = isr_best - cpu_d[p];
                                        if (dist < 0) dist = -dist;
                                        if (dist < best_dist) {
                                            best_dist = dist;
                                            isr_p = p;
                                        }
                                    }
                                    if (isr_p >= 0 && isr_p < 4)
                                        isr_votes[isr_p]++;
                                } else {
                                    isr_pkt_timeout++;
                                }
                            }

                            /* Core classification */
                            int core_best = -9999, core_pred = 0;
                            for (int p = 0; p < TVOX_K; p++) {
                                int d = 0;
                                for (int j = 0; j < CFC_INPUT_DIM; j++) {
                                    if (sig[p][j] != T_ZERO && cfc.input[j] != T_ZERO)
                                        d += tmul(sig[p][j], cfc.input[j]);
                                }
                                if (d > core_best) { core_best = d; core_pred = p; }
                            }

                            /* Novelty gate on core */
                            if (core_best < NOVELTY_THRESHOLD) {
                                novel_in_window++;
                                total_novel++;
                                goto next_pkt;
                            }

                            core_votes[core_pred]++;
                            total_classified++;

                            /* Online core signature maintenance */
                            for (int j = 0; j < CFC_INPUT_DIM; j++)
                                sig_sum[core_pred][j] += cfc.input[j];
                            sig_count[core_pred]++;
                            if (sig_count[core_pred] % RESIGN_INTERVAL == 0) {
                                for (int j = 0; j < CFC_INPUT_DIM; j++) {
                                    sig[core_pred][j] = tsign(sig_sum[core_pred][j]);
                                    sig_sum[core_pred][j] /= 2;
                                }
                                /* Sync W_f weights with updated signature so ISR
                                 * computes the same dots as the CPU core path.
                                 * All 8 neurons for this pattern get the new sig. */
                                for (int nn = core_pred * 8; nn < core_pred * 8 + 8; nn++) {
                                    for (int j = 0; j < CFC_INPUT_DIM; j++)
                                        cfc.W_f[nn][j] = sig[core_pred][j];
                                }
                                /* Update cube[0] to match */
                                memcpy(cube[0].sig, sig, sizeof(sig));
                                total_resigns++;
                            }

                            /* XOR masks: faces as intervention sensors.
                             * Don't vote. Compute the MASK — where does each
                             * face's signature for this pattern DIFFER from core?
                             * mask = positions where core_sig[p] != face_sig[p]
                             * The mask IS the temporal displacement signal.
                             * Reversible: core XOR mask = face, face XOR mask = core. */
                            /* Core always wins — faces are sensors, not voters */

                            /* Accumulate XOR mask stats per face for this pattern */
                            for (int v = 1; v < TVOX_TOTAL; v++) {
                                if (cube[v].sig_count[core_pred] < 2) continue;
                                int mask_weight = 0;  /* Hamming = # trits in mask */
                                int mask_rssi = 0;    /* trits [0..15] */
                                int mask_patid = 0;   /* trits [16..23] */
                                int mask_payload = 0; /* trits [24..87] */
                                int mask_timing = 0;  /* trits [88..127] */
                                for (int j = 0; j < CFC_INPUT_DIM; j++) {
                                    if (cube[v].sig[core_pred][j] !=
                                        sig[core_pred][j]) {
                                        mask_weight++;
                                        if (j < 16)       mask_rssi++;
                                        else if (j < 24)  mask_patid++;
                                        else if (j < 88)  mask_payload++;
                                        else               mask_timing++;
                                    }
                                }
                                /* Accumulate into window stats */
                                xor_mask_total[v] += mask_weight;
                                xor_mask_rssi[v] += mask_rssi;
                                xor_mask_patid[v] += mask_patid;
                                xor_mask_payload[v] += mask_payload;
                                xor_mask_timing[v] += mask_timing;
                                xor_mask_count[v]++;
                            }

                            /* Online face signature maintenance */
                            for (int v = 1; v < TVOX_TOTAL; v++) {
                                if (cube[v].total_samples > 0) {
                                    for (int j = 0; j < CFC_INPUT_DIM; j++)
                                        cube[v].sig_sum[core_pred][j] += cfc.input[j];
                                    cube[v].sig_count[core_pred]++;
                                    cube[v].total_samples++;
                                    /* Re-sign faces periodically */
                                    if (cube[v].sig_count[core_pred] % (RESIGN_INTERVAL * 2) == 0) {
                                        for (int j = 0; j < CFC_INPUT_DIM; j++) {
                                            cube[v].sig[core_pred][j] =
                                                tsign(cube[v].sig_sum[core_pred][j]);
                                            cube[v].sig_sum[core_pred][j] /= 2;
                                        }
                                    }
                                }
                            }
                        }
                        next_pkt:
                        if (drain_buf[i].pkt.pattern_id < 4)
                            test_votes[drain_buf[i].pkt.pattern_id]++;
                        test_packets++;

                        if (prev_pkt_us > 0) {
                            int64_t gap = (drain_buf[i].rx_timestamp_us - prev_pkt_us) / 1000;
                            gap_sum_ms += gap;
                            gap_count++;
                        }
                        prev_pkt_us = drain_buf[i].rx_timestamp_us;
                    }
                }

                /* Novel window check */
                int classified_in_window = core_votes[0] + core_votes[1] +
                                           core_votes[2] + core_votes[3];
                if (novel_in_window > classified_in_window && novel_in_window > 0) {
                    novel_windows++;
                    continue;
                }

                /* Ground truth */
                int truth = -1, truth_votes = 0;
                for (int p = 0; p < 4; p++) {
                    if (test_votes[p] > truth_votes) {
                        truth_votes = test_votes[p];
                        truth = p;
                    }
                }
                if (truth < 0 || truth_votes <= test_packets / 2 ||
                    test_packets < 1 || classified_in_window < 1) continue;

                /* Core prediction (majority of core per-packet votes) */
                int c_pred = 0;
                for (int p = 1; p < 4; p++)
                    if (core_votes[p] > core_votes[c_pred]) c_pred = p;
                if (c_pred == truth) core_correct++;

                /* ISR-level TriX prediction (per-packet votes accumulated) */
                int isr_pred = 0;
                for (int p = 1; p < 4; p++)
                    if (isr_votes[p] > isr_votes[isr_pred]) isr_pred = p;
                int isr_total_votes = isr_votes[0] + isr_votes[1] + isr_votes[2] + isr_votes[3];
                if (isr_total_votes > 0) {
                    isr_attempted++;  /* window had at least one clean ISR vote */
                    if (isr_pred == truth) isr_correct++;
                }

                /* Baseline */
                int baseline_pred = -1;
                if (gap_count > 0) {
                    int avg_gap = (int)(gap_sum_ms / gap_count);
                    if (avg_gap < 130)       baseline_pred = 0;
                    else if (avg_gap < 300)  baseline_pred = 1;
                    else                     baseline_pred = 2;
                }
                if (baseline_pred == truth) baseline_correct++;

                sig_total++;

                /* Accumulate window XOR masks into global */
                for (int v = 1; v < TVOX_TOTAL; v++) {
                    g_xor_total[v] += xor_mask_total[v];
                    g_xor_rssi[v] += xor_mask_rssi[v];
                    g_xor_patid[v] += xor_mask_patid[v];
                    g_xor_payload[v] += xor_mask_payload[v];
                    g_xor_timing[v] += xor_mask_timing[v];
                    g_xor_count[v] += xor_mask_count[v];
                }

                /* Per-sample: core result + ISR result + XOR mask summary */
                printf("    s%02d: truth=%d core=%d isr=%d | cv=[%d,%d,%d,%d] iv=[%d,%d,%d,%d]",
                       sig_total, truth, c_pred, isr_pred,
                       core_votes[0], core_votes[1],
                       core_votes[2], core_votes[3],
                       isr_votes[0], isr_votes[1],
                       isr_votes[2], isr_votes[3]);
                /* Show XOR mask weight per face (avg trits displaced) */
                printf(" xor=[");
                for (int v = 1; v < TVOX_TOTAL; v++) {
                    if (xor_mask_count[v] > 0)
                        printf("%d", xor_mask_total[v] / xor_mask_count[v]);
                    else
                        printf("-");
                    if (v < TVOX_TOTAL - 1) printf(",");
                }
                printf("]\n");
                fflush(stdout);
            }

            stop_freerun();

            int core_pct = (sig_total > 0) ? (core_correct * 100 / sig_total) : 0;
            int isr_pct = (sig_total > 0) ? (isr_correct * 100 / sig_total) : 0;
            int isr_clean_pct = (isr_attempted > 0) ? (isr_correct * 100 / isr_attempted) : 0;
            int base_pct = (sig_total > 0) ? (baseline_correct * 100 / sig_total) : 0;

            printf("\n  ═══ CLASSIFICATION RESULTS (TriX Cube) ═══\n");
            printf("  Total GIE loops: %d\n", (int)loop_count);
            printf("  TriX ISR classifications: %d (at %d Hz)\n",
                   (int)trix_count, sig_total > 0 ? (int)(trix_count * 1000000LL /
                   (esp_timer_get_time() - test_start)) : 0);
            printf("  Test samples: %d\n", sig_total);
            printf("  Ring drops: %d\n", (int)espnow_ring_drops());
            printf("  Core (CPU per-pkt vote):   %d/%d = %d%%\n",
                   core_correct, sig_total, core_pct);
            printf("  ISR  (HW 430 Hz TriX):     %d/%d = %d%% (all samples)\n",
                   isr_correct, sig_total, isr_pct);
            printf("  ISR  (windows w/ data):    %d/%d = %d%% (%d pkts clean, %d pkts timeout)\n",
                   isr_correct, isr_attempted, isr_clean_pct, isr_pkt_clean, isr_pkt_timeout);
            printf("  TriX channel signals:      %d (seq=%d)\n",
                   (int)trix_count, (int)trix_channel.sequence);
            printf("  Baseline (packet-rate):    %d/%d = %d%%\n",
                   baseline_correct, sig_total, base_pct);

            /* Novelty stats */
            printf("\n  ── Novelty Detection ──\n");
            printf("  Novel packets: %d / %d (%d%%)\n",
                   total_novel, total_novel + total_classified,
                   (total_novel + total_classified) > 0
                       ? (total_novel * 100 / (total_novel + total_classified)) : 0);
            printf("  Novel windows: %d\n", novel_windows);

            /* Online maintenance */
            printf("\n  ── Online Maintenance ──\n");
            printf("  Core re-signs: %d\n", total_resigns);
            printf("  Core drift (Hamming):\n");
            for (int p = 0; p < NUM_TEMPLATES; p++) {
                int drift = trit_hamming(sig_orig[p], sig[p], CFC_INPUT_DIM);
                printf("    sig[%d]: %d/%d trits\n", p, drift, CFC_INPUT_DIM);
            }

            /* XOR Mask Analysis — the temporal displacement signal */
            printf("\n  ── XOR Masks (temporal displacement per face) ──\n");
            printf("    Face              avg  | rssi patid payload timing\n");
            for (int v = 1; v < TVOX_TOTAL; v++) {
                if (g_xor_count[v] > 0) {
                    int avg = g_xor_total[v] / g_xor_count[v];
                    int ar  = g_xor_rssi[v] / g_xor_count[v];
                    int ap  = g_xor_patid[v] / g_xor_count[v];
                    int apl = g_xor_payload[v] / g_xor_count[v];
                    int at  = g_xor_timing[v] / g_xor_count[v];
                    printf("    %-18s %3d/128 | %2d/16 %2d/8  %2d/64   %2d/40\n",
                           face_names[v], avg, ar, ap, apl, at);
                } else {
                    printf("    %-18s   -     | (no data)\n", face_names[v]);
                }
            }
            /* Which trit regions carry the temporal signal? */
            int total_xor_all = 0, total_xor_rssi = 0, total_xor_patid = 0;
            int total_xor_payload = 0, total_xor_timing = 0, total_xor_n = 0;
            for (int v = 1; v < TVOX_TOTAL; v++) {
                total_xor_all += g_xor_total[v];
                total_xor_rssi += g_xor_rssi[v];
                total_xor_patid += g_xor_patid[v];
                total_xor_payload += g_xor_payload[v];
                total_xor_timing += g_xor_timing[v];
                total_xor_n += g_xor_count[v];
            }
            if (total_xor_n > 0) {
                printf("    ── Signal distribution ──\n");
                printf("    RSSI:    %d%% of mask weight\n",
                       total_xor_all > 0 ? total_xor_rssi * 100 / total_xor_all : 0);
                printf("    PatID:   %d%% of mask weight\n",
                       total_xor_all > 0 ? total_xor_patid * 100 / total_xor_all : 0);
                printf("    Payload: %d%% of mask weight\n",
                       total_xor_all > 0 ? total_xor_payload * 100 / total_xor_all : 0);
                printf("    Timing:  %d%% of mask weight\n",
                       total_xor_all > 0 ? total_xor_timing * 100 / total_xor_all : 0);
            }

            /* Face final divergence from core */
            printf("\n  ── TriX Cube Geometry ──\n");
            for (int v = 1; v < TVOX_TOTAL; v++) {
                int total_hamm = 0, counted = 0;
                for (int p = 0; p < TVOX_K; p++) {
                    if (cube[v].sig_count[p] > 0) {
                        total_hamm += trit_hamming(sig[p],
                                                   cube[v].sig[p], CFC_INPUT_DIM);
                        counted++;
                    }
                }
                printf("    %s: %d samples, %d avg hamming from core\n",
                       face_names[v], cube[v].total_samples,
                       counted > 0 ? total_hamm / counted : 0);
            }

            /* Gate selectivity */
            int final_fire_pct = (gate_steps_total > 0)
                ? (int)(gate_fires_total * 100LL / gate_steps_total) : 0;
            printf("  Gate firing: %d%%\n", final_fire_pct);

            printf("\n  Architecture:\n");
            printf("  - TriX ISR: HW classifies at 430 Hz (CfC blend DISABLED — Phase 3)\n");
            printf("  - 7-voxel TriX Cube: core + 6 temporal faces\n");
            printf("  - Faces: recent, prior, stable, transient, confident, uncertain\n");
            printf("  - Core classifies alone. Faces compute XOR masks (intervention sensors)\n");
            printf("  - XOR mask = trit positions where face sig != core sig\n");
            printf("  - Online maintenance + novelty detection\n");
            printf("  - Gate threshold=%d, novelty=%d\n",
                   (int)gate_threshold, NOVELTY_THRESHOLD);

            int ok = (sig_total >= 4) && (core_pct >= 50);
            printf("  %s\n\n", ok ? "OK" : "FAIL");
            fflush(stdout);
            return ok;
        }
        return 0; /* unreachable */
    }
    return 0; /* unreachable */
}
