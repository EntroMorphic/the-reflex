/**
 * obsbot_test.c - OBSBOT Camera Test Utility
 *
 * Test program to verify OBSBOT PTZ control on Linux (Pi 4, Thor, etc.)
 *
 * Build:
 *   gcc -o obsbot_test obsbot_test.c -O2
 *
 * Usage:
 *   ./obsbot_test                    # Auto-detect and test
 *   ./obsbot_test /dev/video0        # Test specific device
 *   ./obsbot_test --scan             # Scan for all cameras
 *   ./obsbot_test --demo             # Run interactive demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

#include "../include/reflex_obsbot.h"

// =============================================================================
// NON-BLOCKING KEYBOARD INPUT
// =============================================================================

static struct termios orig_termios;

void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;  // 100ms timeout

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// =============================================================================
// TEST FUNCTIONS
// =============================================================================

void test_basic_ptz(reflex_obsbot_t* cam) {
    printf("\n=== Basic PTZ Test ===\n");

    printf("Moving to home position...\n");
    obsbot_home(cam);
    usleep(1000000);

    printf("Pan right 30 degrees...\n");
    obsbot_look_deg(cam, 30.0f, 0.0f);
    usleep(1000000);

    printf("Pan left 30 degrees...\n");
    obsbot_look_deg(cam, -30.0f, 0.0f);
    usleep(1000000);

    printf("Tilt up 20 degrees...\n");
    obsbot_look_deg(cam, 0.0f, 20.0f);
    usleep(1000000);

    printf("Tilt down 20 degrees...\n");
    obsbot_look_deg(cam, 0.0f, -20.0f);
    usleep(1000000);

    printf("Zoom to 2x...\n");
    obsbot_zoom(cam, 200);
    usleep(1000000);

    printf("Zoom to 1x...\n");
    obsbot_zoom(cam, 100);
    usleep(500000);

    printf("Returning home...\n");
    obsbot_home(cam);

    printf("\nBasic test complete.\n");
    obsbot_print_stats(cam);
}

void test_latency(reflex_obsbot_t* cam) {
    printf("\n=== Latency Test ===\n");
    printf("Sending 100 PTZ commands...\n");

    for (int i = 0; i < 100; i++) {
        int32_t pan = (i % 2 == 0) ? DEG_TO_ARCSEC(5) : DEG_TO_ARCSEC(-5);
        obsbot_pan(cam, pan);
        usleep(10000);  // 10ms between commands
    }

    obsbot_home(cam);

    printf("\nLatency results:\n");
    printf("  Total commands: %lu\n", (unsigned long)cam->total_commands);
    printf("  Avg latency:    %.2f us\n", obsbot_avg_latency_ns(cam) / 1000.0f);
    printf("  Last command:   %.2f us\n", cam->last_command_ns / 1000.0f);
}

void test_sweep(reflex_obsbot_t* cam) {
    printf("\n=== Sweep Test ===\n");
    printf("Sweeping through pan range...\n");

    for (int deg = -60; deg <= 60; deg += 5) {
        obsbot_look_deg(cam, (float)deg, 0.0f);
        printf("  pan: %d°\n", deg);
        usleep(100000);
    }

    printf("Sweeping through tilt range...\n");
    obsbot_pan(cam, 0);

    for (int deg = -30; deg <= 30; deg += 5) {
        obsbot_look_deg(cam, 0.0f, (float)deg);
        printf("  tilt: %d°\n", deg);
        usleep(100000);
    }

    obsbot_home(cam);
    printf("Sweep complete.\n");
}

void run_interactive_demo(reflex_obsbot_t* cam) {
    printf("\n=== Interactive Demo ===\n");
    printf("Controls:\n");
    printf("  Arrow keys: Pan/Tilt\n");
    printf("  +/-:        Zoom in/out\n");
    printf("  h:          Home\n");
    printf("  s:          Print stats\n");
    printf("  q:          Quit\n");
    printf("\n");

    enable_raw_mode();

    int32_t pan = 0, tilt = 0, zoom = 100;
    const int32_t PAN_STEP = DEG_TO_ARCSEC(5);
    const int32_t TILT_STEP = DEG_TO_ARCSEC(5);
    const int32_t ZOOM_STEP = 20;

    char c;
    while (1) {
        if (read(STDIN_FILENO, &c, 1) == 1) {
            if (c == 'q' || c == 'Q') break;

            switch (c) {
                case 'h': case 'H':
                    pan = tilt = 0; zoom = 100;
                    obsbot_home(cam);
                    printf("Home\n");
                    break;

                case 's': case 'S':
                    obsbot_print_stats(cam);
                    break;

                case '+': case '=':
                    zoom += ZOOM_STEP;
                    if (zoom > cam->zoom_max) zoom = cam->zoom_max;
                    obsbot_zoom(cam, zoom);
                    printf("Zoom: %d\n", zoom);
                    break;

                case '-': case '_':
                    zoom -= ZOOM_STEP;
                    if (zoom < cam->zoom_min) zoom = cam->zoom_min;
                    obsbot_zoom(cam, zoom);
                    printf("Zoom: %d\n", zoom);
                    break;

                case 27:  // Escape sequence (arrow keys)
                    if (read(STDIN_FILENO, &c, 1) == 1 && c == '[') {
                        if (read(STDIN_FILENO, &c, 1) == 1) {
                            switch (c) {
                                case 'A':  // Up
                                    tilt += TILT_STEP;
                                    obsbot_tilt(cam, tilt);
                                    printf("Tilt: %.1f°\n", ARCSEC_TO_DEG(tilt));
                                    break;
                                case 'B':  // Down
                                    tilt -= TILT_STEP;
                                    obsbot_tilt(cam, tilt);
                                    printf("Tilt: %.1f°\n", ARCSEC_TO_DEG(tilt));
                                    break;
                                case 'C':  // Right
                                    pan += PAN_STEP;
                                    obsbot_pan(cam, pan);
                                    printf("Pan: %.1f°\n", ARCSEC_TO_DEG(pan));
                                    break;
                                case 'D':  // Left
                                    pan -= PAN_STEP;
                                    obsbot_pan(cam, pan);
                                    printf("Pan: %.1f°\n", ARCSEC_TO_DEG(pan));
                                    break;
                            }
                        }
                    }
                    break;
            }
        }
    }

    disable_raw_mode();
    printf("\nDemo ended.\n");
}

void scan_cameras(void) {
    printf("Scanning for cameras...\n\n");

    reflex_obsbot_t cameras[10];
    int found = obsbot_auto_detect(cameras, 10);

    printf("\nFound %d PTZ-capable camera(s)\n", found);

    for (int i = 0; i < found; i++) {
        printf("\nCamera %d: %s\n", i, cameras[i].device_path);
        obsbot_list_controls(&cameras[i]);
        obsbot_close(&cameras[i]);
    }
}

// =============================================================================
// MAIN
// =============================================================================

void print_usage(const char* prog) {
    printf("Usage: %s [options] [device]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  --scan        Scan for all cameras\n");
    printf("  --demo        Run interactive demo\n");
    printf("  --sweep       Run sweep test\n");
    printf("  --latency     Run latency test\n");
    printf("  --list        List controls on device\n");
    printf("  --help        Show this help\n");
    printf("\n");
    printf("Device:\n");
    printf("  /dev/videoN   Specific camera device\n");
    printf("  (default)     Auto-detect first PTZ camera\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s                      # Auto-detect and run basic test\n", prog);
    printf("  %s --demo               # Interactive keyboard control\n", prog);
    printf("  %s /dev/video0 --sweep  # Sweep test on specific camera\n", prog);
}

int main(int argc, char* argv[]) {
    const char* device = NULL;
    int do_demo = 0;
    int do_scan = 0;
    int do_sweep = 0;
    int do_latency = 0;
    int do_list = 0;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--demo") == 0) {
            do_demo = 1;
        } else if (strcmp(argv[i], "--scan") == 0) {
            do_scan = 1;
        } else if (strcmp(argv[i], "--sweep") == 0) {
            do_sweep = 1;
        } else if (strcmp(argv[i], "--latency") == 0) {
            do_latency = 1;
        } else if (strcmp(argv[i], "--list") == 0) {
            do_list = 1;
        } else if (argv[i][0] == '/') {
            device = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    printf("OBSBOT Reflex Test Utility\n");
    printf("==========================\n");

    // Scan mode
    if (do_scan) {
        scan_cameras();
        return 0;
    }

    // Initialize camera (with wake from USB suspend)
    reflex_obsbot_t cam;

    if (device) {
        if (obsbot_init_wake(&cam, device) != OBSBOT_OK) {
            fprintf(stderr, "Failed to initialize %s\n", device);
            return 1;
        }
    } else {
        printf("Auto-detecting camera...\n");
        reflex_obsbot_t cameras[10];
        int found = obsbot_auto_detect(cameras, 10);

        if (found == 0) {
            fprintf(stderr, "No PTZ cameras found.\n");
            fprintf(stderr, "Try: v4l2-ctl --list-devices\n");
            return 1;
        }

        cam = cameras[0];
        printf("Using: %s\n", cam.device_path);

        // Wake camera from USB suspend
        obsbot_wake(&cam);

        // Close others
        for (int i = 1; i < found; i++) {
            obsbot_close(&cameras[i]);
        }
    }

    // List controls
    if (do_list) {
        obsbot_list_controls(&cam);
        obsbot_close(&cam);
        return 0;
    }

    // Run tests
    if (do_demo) {
        run_interactive_demo(&cam);
    } else if (do_sweep) {
        test_sweep(&cam);
    } else if (do_latency) {
        test_latency(&cam);
    } else {
        // Default: basic test
        test_basic_ptz(&cam);
    }

    obsbot_close(&cam);
    return 0;
}
