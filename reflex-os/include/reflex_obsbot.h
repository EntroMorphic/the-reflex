/**
 * reflex_obsbot.h - OBSBOT Tiny PTZ Camera Control via Reflex
 *
 * Sub-microsecond camera control integrated with entropy field attention.
 * The eyes of the Dreaming Swarm Cathedral.
 *
 * Hardware: OBSBOT Tiny (VID: 0x3564 or 0x6e30, PID: 0xfef0)
 * Interface: V4L2 UVC PTZ controls + USB fallback
 * Platform: Linux (Pi 4, Jetson Thor)
 *
 * Usage:
 *   reflex_obsbot_t left_eye, right_eye;
 *   obsbot_init(&left_eye, "/dev/video0");
 *   obsbot_init(&right_eye, "/dev/video2");
 *
 *   // Direct control
 *   obsbot_pan_tilt(&left_eye, 18000, -9000);  // 50° right, 25° down
 *   obsbot_zoom(&left_eye, 200);               // 2x zoom
 *
 *   // Entropy-driven attention
 *   obsbot_stereo_t eyes = { &left_eye, &right_eye, 5000 };
 *   obsbot_track_entropy(&eyes, &entropy_field);
 */

#ifndef REFLEX_OBSBOT_H
#define REFLEX_OBSBOT_H

#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// TIMING (Linux high-resolution)
// =============================================================================

static inline uint64_t reflex_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// =============================================================================
// REFLEX CHANNEL (Simplified for Linux)
// =============================================================================

typedef struct {
    int32_t value;
    uint64_t timestamp_ns;
    uint32_t sequence;
} reflex_channel_t;

static inline void reflex_signal(reflex_channel_t* ch, int32_t value) {
    ch->value = value;
    ch->timestamp_ns = reflex_nanos();
    __sync_fetch_and_add(&ch->sequence, 1);
}

// =============================================================================
// OBSBOT CONSTANTS
// =============================================================================

#define OBSBOT_VID              0x6e30
#define OBSBOT_PID              0xfef0

// UVC PTZ uses arc-seconds: 1 arc-second = 1/3600 degree
#define OBSBOT_PAN_MIN          (-180 * 3600)   // -180°
#define OBSBOT_PAN_MAX          (180 * 3600)    // +180°
#define OBSBOT_TILT_MIN         (-180 * 3600)   // -180° (camera may limit)
#define OBSBOT_TILT_MAX         (180 * 3600)    // +180°
#define OBSBOT_ZOOM_MIN         100             // 1x
#define OBSBOT_ZOOM_MAX         500             // 5x (model dependent)

// Practical limits for OBSBOT Tiny
#define OBSBOT_TINY_PAN_RANGE   (120 * 3600)    // ±120° actual range
#define OBSBOT_TINY_TILT_RANGE  (90 * 3600)     // ±90° actual range

// =============================================================================
// OBSBOT CAMERA STRUCTURE
// =============================================================================

typedef enum {
    OBSBOT_OK = 0,
    OBSBOT_ERR_OPEN = -1,
    OBSBOT_ERR_NO_PTZ = -2,
    OBSBOT_ERR_IOCTL = -3,
    OBSBOT_ERR_RANGE = -4,
} obsbot_error_t;

typedef struct {
    // Device
    int fd;
    char device_path[64];
    bool initialized;

    // Capabilities
    bool has_pan;
    bool has_tilt;
    bool has_zoom;
    bool has_pan_speed;
    bool has_tilt_speed;

    // Current state (cached)
    int32_t pan;            // Arc-seconds
    int32_t tilt;           // Arc-seconds
    int32_t zoom;           // 100-500

    // Limits (queried from device)
    int32_t pan_min, pan_max;
    int32_t tilt_min, tilt_max;
    int32_t zoom_min, zoom_max;

    // Reflex channels for coordination
    reflex_channel_t ch_pan;
    reflex_channel_t ch_tilt;
    reflex_channel_t ch_zoom;
    reflex_channel_t ch_frame;      // Frame arrival signal

    // Timing stats
    uint64_t last_command_ns;
    uint64_t total_commands;
    uint64_t total_latency_ns;
} reflex_obsbot_t;

// =============================================================================
// STEREO VISION PAIR
// =============================================================================

typedef struct {
    reflex_obsbot_t* left;
    reflex_obsbot_t* right;
    int32_t vergence_offset;    // Arc-seconds between eyes (stereo convergence)
    int32_t baseline_mm;        // Physical separation in mm
} obsbot_stereo_t;

// =============================================================================
// INITIALIZATION
// =============================================================================

/**
 * Query a V4L2 control's range
 */
static inline bool obsbot_query_control(int fd, uint32_t id,
                                         int32_t* min, int32_t* max, int32_t* def) {
    struct v4l2_queryctrl query;
    memset(&query, 0, sizeof(query));
    query.id = id;

    if (ioctl(fd, VIDIOC_QUERYCTRL, &query) < 0) {
        return false;
    }

    if (min) *min = query.minimum;
    if (max) *max = query.maximum;
    if (def) *def = query.default_value;
    return true;
}

/**
 * Initialize OBSBOT camera
 *
 * @param cam       Camera structure to initialize
 * @param device    V4L2 device path (e.g., "/dev/video0")
 * @return          OBSBOT_OK on success, error code otherwise
 */
static inline obsbot_error_t obsbot_init(reflex_obsbot_t* cam, const char* device) {
    memset(cam, 0, sizeof(*cam));
    strncpy(cam->device_path, device, sizeof(cam->device_path) - 1);

    // Open device
    cam->fd = open(device, O_RDWR | O_NONBLOCK);
    if (cam->fd < 0) {
        fprintf(stderr, "obsbot: failed to open %s: %s\n", device, strerror(errno));
        return OBSBOT_ERR_OPEN;
    }

    // Query PTZ capabilities
    cam->has_pan = obsbot_query_control(cam->fd, V4L2_CID_PAN_ABSOLUTE,
                                         &cam->pan_min, &cam->pan_max, &cam->pan);
    cam->has_tilt = obsbot_query_control(cam->fd, V4L2_CID_TILT_ABSOLUTE,
                                          &cam->tilt_min, &cam->tilt_max, &cam->tilt);
    cam->has_zoom = obsbot_query_control(cam->fd, V4L2_CID_ZOOM_ABSOLUTE,
                                          &cam->zoom_min, &cam->zoom_max, &cam->zoom);

    // Also check for speed controls (relative movement)
    int32_t dummy;
    cam->has_pan_speed = obsbot_query_control(cam->fd, V4L2_CID_PAN_SPEED,
                                               &dummy, &dummy, &dummy);
    cam->has_tilt_speed = obsbot_query_control(cam->fd, V4L2_CID_TILT_SPEED,
                                                &dummy, &dummy, &dummy);

    if (!cam->has_pan && !cam->has_tilt && !cam->has_zoom) {
        fprintf(stderr, "obsbot: no PTZ controls found on %s\n", device);
        fprintf(stderr, "obsbot: try 'v4l2-ctl -d %s --list-ctrls' to see available controls\n", device);
        close(cam->fd);
        cam->fd = -1;
        return OBSBOT_ERR_NO_PTZ;
    }

    cam->initialized = true;

    printf("obsbot: initialized %s\n", device);
    printf("  pan:  %s (range: %d to %d arc-sec)\n",
           cam->has_pan ? "YES" : "no", cam->pan_min, cam->pan_max);
    printf("  tilt: %s (range: %d to %d arc-sec)\n",
           cam->has_tilt ? "YES" : "no", cam->tilt_min, cam->tilt_max);
    printf("  zoom: %s (range: %d to %d)\n",
           cam->has_zoom ? "YES" : "no", cam->zoom_min, cam->zoom_max);
    printf("  pan_speed:  %s\n", cam->has_pan_speed ? "YES" : "no");
    printf("  tilt_speed: %s\n", cam->has_tilt_speed ? "YES" : "no");

    return OBSBOT_OK;
}

/**
 * Close camera
 */
static inline void obsbot_close(reflex_obsbot_t* cam) {
    if (cam->fd >= 0) {
        close(cam->fd);
        cam->fd = -1;
    }
    cam->initialized = false;
}

/**
 * Wake camera from USB suspend
 *
 * OBSBOT cameras enter low-power mode when not streaming.
 * PTZ commands are accepted but not executed until camera wakes.
 * This function requests a frame to wake the camera.
 *
 * @param cam   Camera to wake
 * @return      OBSBOT_OK on success
 */
static inline obsbot_error_t obsbot_wake(reflex_obsbot_t* cam) {
    if (!cam->initialized || cam->fd < 0) return OBSBOT_ERR_OPEN;

    // Request streaming capability check - this wakes the camera
    struct v4l2_capability cap;
    if (ioctl(cam->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        return OBSBOT_ERR_IOCTL;
    }

    // Request a format query - further ensures camera is active
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(cam->fd, VIDIOC_G_FMT, &fmt);

    // Small delay for camera to fully wake
    usleep(100000);  // 100ms

    printf("obsbot: camera woken from suspend\n");
    return OBSBOT_OK;
}

/**
 * Initialize and wake camera (convenience)
 */
static inline obsbot_error_t obsbot_init_wake(reflex_obsbot_t* cam, const char* device) {
    obsbot_error_t err = obsbot_init(cam, device);
    if (err != OBSBOT_OK) return err;
    return obsbot_wake(cam);
}

// =============================================================================
// DIRECT CONTROL (Low-level)
// =============================================================================

/**
 * Set a V4L2 control value
 */
static inline obsbot_error_t obsbot_set_control(reflex_obsbot_t* cam,
                                                 uint32_t id, int32_t value) {
    struct v4l2_control ctrl;
    ctrl.id = id;
    ctrl.value = value;

    uint64_t t0 = reflex_nanos();

    if (ioctl(cam->fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        fprintf(stderr, "obsbot: ioctl failed for control 0x%x: %s\n",
                id, strerror(errno));
        return OBSBOT_ERR_IOCTL;
    }

    uint64_t latency = reflex_nanos() - t0;
    cam->last_command_ns = latency;
    cam->total_commands++;
    cam->total_latency_ns += latency;

    return OBSBOT_OK;
}

/**
 * Get a V4L2 control value
 */
static inline obsbot_error_t obsbot_get_control(reflex_obsbot_t* cam,
                                                 uint32_t id, int32_t* value) {
    struct v4l2_control ctrl;
    ctrl.id = id;

    if (ioctl(cam->fd, VIDIOC_G_CTRL, &ctrl) < 0) {
        return OBSBOT_ERR_IOCTL;
    }

    *value = ctrl.value;
    return OBSBOT_OK;
}

// =============================================================================
// PTZ CONTROL (High-level)
// =============================================================================

/**
 * Set pan position (absolute)
 *
 * @param cam   Camera
 * @param pan   Pan in arc-seconds (-648000 to +648000 for ±180°)
 */
static inline obsbot_error_t obsbot_pan(reflex_obsbot_t* cam, int32_t pan) {
    if (!cam->has_pan) return OBSBOT_ERR_NO_PTZ;

    // Clamp to device limits
    if (pan < cam->pan_min) pan = cam->pan_min;
    if (pan > cam->pan_max) pan = cam->pan_max;

    obsbot_error_t err = obsbot_set_control(cam, V4L2_CID_PAN_ABSOLUTE, pan);
    if (err == OBSBOT_OK) {
        cam->pan = pan;
        reflex_signal(&cam->ch_pan, pan);
    }
    return err;
}

/**
 * Set tilt position (absolute)
 */
static inline obsbot_error_t obsbot_tilt(reflex_obsbot_t* cam, int32_t tilt) {
    if (!cam->has_tilt) return OBSBOT_ERR_NO_PTZ;

    if (tilt < cam->tilt_min) tilt = cam->tilt_min;
    if (tilt > cam->tilt_max) tilt = cam->tilt_max;

    obsbot_error_t err = obsbot_set_control(cam, V4L2_CID_TILT_ABSOLUTE, tilt);
    if (err == OBSBOT_OK) {
        cam->tilt = tilt;
        reflex_signal(&cam->ch_tilt, tilt);
    }
    return err;
}

/**
 * Set both pan and tilt
 */
static inline obsbot_error_t obsbot_pan_tilt(reflex_obsbot_t* cam,
                                              int32_t pan, int32_t tilt) {
    obsbot_error_t err1 = obsbot_pan(cam, pan);
    obsbot_error_t err2 = obsbot_tilt(cam, tilt);
    return (err1 != OBSBOT_OK) ? err1 : err2;
}

/**
 * Set zoom level
 *
 * @param cam   Camera
 * @param zoom  Zoom level (100 = 1x, 200 = 2x, etc.)
 */
static inline obsbot_error_t obsbot_zoom(reflex_obsbot_t* cam, int32_t zoom) {
    if (!cam->has_zoom) return OBSBOT_ERR_NO_PTZ;

    if (zoom < cam->zoom_min) zoom = cam->zoom_min;
    if (zoom > cam->zoom_max) zoom = cam->zoom_max;

    obsbot_error_t err = obsbot_set_control(cam, V4L2_CID_ZOOM_ABSOLUTE, zoom);
    if (err == OBSBOT_OK) {
        cam->zoom = zoom;
        reflex_signal(&cam->ch_zoom, zoom);
    }
    return err;
}

/**
 * Set all PTZ at once
 */
static inline obsbot_error_t obsbot_set_ptz(reflex_obsbot_t* cam,
                                             int32_t pan, int32_t tilt, int32_t zoom) {
    obsbot_error_t err;
    err = obsbot_pan(cam, pan);
    if (err != OBSBOT_OK) return err;
    err = obsbot_tilt(cam, tilt);
    if (err != OBSBOT_OK) return err;
    err = obsbot_zoom(cam, zoom);
    return err;
}

/**
 * Return to home position
 */
static inline obsbot_error_t obsbot_home(reflex_obsbot_t* cam) {
    return obsbot_set_ptz(cam, 0, 0, 100);
}

// =============================================================================
// RELATIVE/SPEED CONTROL
// =============================================================================

/**
 * Set pan speed for continuous movement
 * Negative = left, positive = right, 0 = stop
 */
static inline obsbot_error_t obsbot_pan_speed(reflex_obsbot_t* cam, int32_t speed) {
    if (!cam->has_pan_speed) return OBSBOT_ERR_NO_PTZ;
    return obsbot_set_control(cam, V4L2_CID_PAN_SPEED, speed);
}

/**
 * Set tilt speed for continuous movement
 * Negative = down, positive = up, 0 = stop
 */
static inline obsbot_error_t obsbot_tilt_speed(reflex_obsbot_t* cam, int32_t speed) {
    if (!cam->has_tilt_speed) return OBSBOT_ERR_NO_PTZ;
    return obsbot_set_control(cam, V4L2_CID_TILT_SPEED, speed);
}

/**
 * Stop all movement
 */
static inline obsbot_error_t obsbot_stop(reflex_obsbot_t* cam) {
    obsbot_pan_speed(cam, 0);
    obsbot_tilt_speed(cam, 0);
    return OBSBOT_OK;
}

// =============================================================================
// DEGREE HELPERS (More intuitive units)
// =============================================================================

#define DEG_TO_ARCSEC(deg)  ((int32_t)((deg) * 3600))
#define ARCSEC_TO_DEG(as)   ((float)(as) / 3600.0f)

/**
 * Set pan/tilt in degrees (more intuitive)
 */
static inline obsbot_error_t obsbot_look_deg(reflex_obsbot_t* cam,
                                              float pan_deg, float tilt_deg) {
    return obsbot_pan_tilt(cam, DEG_TO_ARCSEC(pan_deg), DEG_TO_ARCSEC(tilt_deg));
}

// =============================================================================
// STEREO VISION
// =============================================================================

/**
 * Initialize stereo pair
 */
static inline obsbot_error_t obsbot_stereo_init(obsbot_stereo_t* stereo,
                                                 reflex_obsbot_t* left,
                                                 reflex_obsbot_t* right,
                                                 int32_t baseline_mm) {
    stereo->left = left;
    stereo->right = right;
    stereo->baseline_mm = baseline_mm;
    stereo->vergence_offset = DEG_TO_ARCSEC(2.0f);  // Default 2° vergence
    return OBSBOT_OK;
}

/**
 * Point both cameras at a target with stereo vergence
 *
 * @param stereo    Stereo camera pair
 * @param pan       Center pan in arc-seconds
 * @param tilt      Center tilt in arc-seconds
 * @param distance  Target distance in mm (for vergence calculation)
 */
static inline obsbot_error_t obsbot_stereo_look(obsbot_stereo_t* stereo,
                                                 int32_t pan, int32_t tilt,
                                                 int32_t distance_mm) {
    // Calculate vergence angle based on distance
    // vergence = 2 * atan(baseline / (2 * distance))
    // Simplified: vergence_deg ≈ (baseline / distance) * (180/π)
    float vergence_deg = 0.0f;
    if (distance_mm > 0) {
        vergence_deg = ((float)stereo->baseline_mm / (float)distance_mm) * 57.3f;
    }
    int32_t vergence = DEG_TO_ARCSEC(vergence_deg);

    // Left eye looks slightly right, right eye looks slightly left
    obsbot_error_t err1 = obsbot_pan_tilt(stereo->left, pan + vergence/2, tilt);
    obsbot_error_t err2 = obsbot_pan_tilt(stereo->right, pan - vergence/2, tilt);

    return (err1 != OBSBOT_OK) ? err1 : err2;
}

/**
 * Point both cameras at same angle (parallel, infinite distance)
 */
static inline obsbot_error_t obsbot_stereo_parallel(obsbot_stereo_t* stereo,
                                                     int32_t pan, int32_t tilt) {
    obsbot_error_t err1 = obsbot_pan_tilt(stereo->left, pan, tilt);
    obsbot_error_t err2 = obsbot_pan_tilt(stereo->right, pan, tilt);
    return (err1 != OBSBOT_OK) ? err1 : err2;
}

/**
 * Home both cameras
 */
static inline obsbot_error_t obsbot_stereo_home(obsbot_stereo_t* stereo) {
    obsbot_home(stereo->left);
    obsbot_home(stereo->right);
    return OBSBOT_OK;
}

// =============================================================================
// ENTROPY FIELD INTEGRATION
// =============================================================================

/**
 * Entropy field cell (minimal definition for integration)
 * Full definition in reflex_void.h
 */
#ifndef REFLEX_VOID_H
typedef struct {
    uint16_t entropy;
    uint16_t activity;
    uint8_t x, y;
} reflex_void_cell_t;

typedef struct {
    reflex_void_cell_t* cells;
    uint8_t width, height;
    uint16_t threshold_critical;
} reflex_entropy_field_t;
#endif

/**
 * Find the point of lowest entropy (highest attention) in the field
 */
static inline void obsbot_find_attention(reflex_entropy_field_t* field,
                                          uint8_t* focus_x, uint8_t* focus_y,
                                          uint16_t* min_entropy) {
    uint16_t min_e = 0xFFFF;
    uint8_t min_x = field->width / 2;
    uint8_t min_y = field->height / 2;

    for (int i = 0; i < field->width * field->height; i++) {
        if (field->cells[i].entropy < min_e) {
            min_e = field->cells[i].entropy;
            min_x = field->cells[i].x;
            min_y = field->cells[i].y;
        }
    }

    *focus_x = min_x;
    *focus_y = min_y;
    if (min_entropy) *min_entropy = min_e;
}

/**
 * Map a value from one range to another
 */
static inline int32_t obsbot_map(int32_t x, int32_t in_min, int32_t in_max,
                                  int32_t out_min, int32_t out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/**
 * Drive stereo cameras based on entropy field attention
 *
 * The cameras track the point of lowest entropy (highest certainty/focus).
 * This implements biological attention: the eyes move to look at what
 * the system is most certain about.
 */
static inline void obsbot_track_entropy(obsbot_stereo_t* stereo,
                                         reflex_entropy_field_t* field) {
    uint8_t focus_x, focus_y;
    uint16_t min_entropy;

    obsbot_find_attention(field, &focus_x, &focus_y, &min_entropy);

    // Map field coordinates to pan/tilt
    // Assuming field center = camera forward
    int32_t pan = obsbot_map(focus_x, 0, field->width - 1,
                              -OBSBOT_TINY_PAN_RANGE/2, OBSBOT_TINY_PAN_RANGE/2);
    int32_t tilt = obsbot_map(focus_y, 0, field->height - 1,
                               OBSBOT_TINY_TILT_RANGE/2, -OBSBOT_TINY_TILT_RANGE/2);

    // Zoom based on entropy: lower entropy = higher zoom (focus harder)
    int32_t zoom = obsbot_map(min_entropy, 0, 0xFFFF, 300, 100);

    // Move both cameras with vergence for nearby attention
    // Assume lower entropy = closer/more important = more vergence
    int32_t distance = obsbot_map(min_entropy, 0, 0xFFFF, 500, 5000);

    obsbot_stereo_look(stereo, pan, tilt, distance);
    obsbot_zoom(stereo->left, zoom);
    obsbot_zoom(stereo->right, zoom);
}

/**
 * Drive a single camera from entropy field
 */
static inline void obsbot_track_entropy_mono(reflex_obsbot_t* cam,
                                              reflex_entropy_field_t* field) {
    uint8_t focus_x, focus_y;
    uint16_t min_entropy;

    obsbot_find_attention(field, &focus_x, &focus_y, &min_entropy);

    int32_t pan = obsbot_map(focus_x, 0, field->width - 1,
                              -OBSBOT_TINY_PAN_RANGE/2, OBSBOT_TINY_PAN_RANGE/2);
    int32_t tilt = obsbot_map(focus_y, 0, field->height - 1,
                               OBSBOT_TINY_TILT_RANGE/2, -OBSBOT_TINY_TILT_RANGE/2);
    int32_t zoom = obsbot_map(min_entropy, 0, 0xFFFF, 300, 100);

    obsbot_set_ptz(cam, pan, tilt, zoom);
}

// =============================================================================
// STATISTICS
// =============================================================================

/**
 * Get average command latency in nanoseconds
 */
static inline uint64_t obsbot_avg_latency_ns(reflex_obsbot_t* cam) {
    if (cam->total_commands == 0) return 0;
    return cam->total_latency_ns / cam->total_commands;
}

/**
 * Print camera statistics
 */
static inline void obsbot_print_stats(reflex_obsbot_t* cam) {
    printf("obsbot stats for %s:\n", cam->device_path);
    printf("  commands:    %lu\n", (unsigned long)cam->total_commands);
    printf("  avg latency: %lu ns (%.2f us)\n",
           (unsigned long)obsbot_avg_latency_ns(cam),
           obsbot_avg_latency_ns(cam) / 1000.0f);
    printf("  last cmd:    %lu ns\n", (unsigned long)cam->last_command_ns);
    printf("  position:    pan=%d tilt=%d zoom=%d\n",
           cam->pan, cam->tilt, cam->zoom);
}

// =============================================================================
// DIAGNOSTIC / DISCOVERY
// =============================================================================

/**
 * List all V4L2 controls on a device (for debugging)
 */
static inline void obsbot_list_controls(reflex_obsbot_t* cam) {
    struct v4l2_queryctrl queryctrl;

    printf("V4L2 controls for %s:\n", cam->device_path);

    memset(&queryctrl, 0, sizeof(queryctrl));
    queryctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;

    while (ioctl(cam->fd, VIDIOC_QUERYCTRL, &queryctrl) == 0) {
        if (!(queryctrl.flags & V4L2_CTRL_FLAG_DISABLED)) {
            printf("  0x%08x: %s (min=%d max=%d step=%d default=%d)\n",
                   queryctrl.id, queryctrl.name,
                   queryctrl.minimum, queryctrl.maximum,
                   queryctrl.step, queryctrl.default_value);
        }
        queryctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }
}

/**
 * Auto-detect OBSBOT cameras
 * Scans /dev/video0 through /dev/video9 for PTZ-capable devices
 */
static inline int obsbot_auto_detect(reflex_obsbot_t* cameras, int max_cameras) {
    int found = 0;
    char path[32];

    for (int i = 0; i < 10 && found < max_cameras; i++) {
        snprintf(path, sizeof(path), "/dev/video%d", i);

        if (obsbot_init(&cameras[found], path) == OBSBOT_OK) {
            found++;
        }
    }

    return found;
}

#ifdef __cplusplus
}
#endif

#endif // REFLEX_OBSBOT_H
