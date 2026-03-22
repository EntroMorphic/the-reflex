/**
 * stereo_demo.c - Synchronized Stereo OBSBOT Controller
 *
 * Keeps both cameras awake and moves them in perfect sync.
 * Arrow keys control both eyes together.
 *
 * Build: gcc -O2 -o stereo_demo stereo_demo.c -lpthread
 * Usage: ./stereo_demo /dev/video0 /dev/video2
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <stdbool.h>
#include <signal.h>

// Camera state
typedef struct {
    int fd;
    const char* name;
    int32_t pan;
    int32_t tilt;
} eye_t;

static eye_t left_eye, right_eye;
static volatile bool running = true;
static struct termios orig_termios;

// Constants
#define PAN_STEP    18000   // 5 degrees
#define TILT_STEP   18000   // 5 degrees
#define PAN_MAX     432000  // 120 degrees
#define TILT_MAX    288000  // 80 degrees

void cleanup(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    printf("\nShutting down...\n");
}

void sigint_handler(int sig) {
    (void)sig;
    running = false;
}

int set_ctrl(int fd, uint32_t id, int32_t value) {
    struct v4l2_control ctrl = { .id = id, .value = value };
    return ioctl(fd, VIDIOC_S_CTRL, &ctrl);
}

void move_eye(eye_t* eye, int32_t pan, int32_t tilt) {
    // Clamp values
    if (pan > PAN_MAX) pan = PAN_MAX;
    if (pan < -PAN_MAX) pan = -PAN_MAX;
    if (tilt > TILT_MAX) tilt = TILT_MAX;
    if (tilt < -TILT_MAX) tilt = -TILT_MAX;

    eye->pan = pan;
    eye->tilt = tilt;

    set_ctrl(eye->fd, V4L2_CID_PAN_ABSOLUTE, pan);
    set_ctrl(eye->fd, V4L2_CID_TILT_ABSOLUTE, tilt);
}

void move_both(int32_t pan, int32_t tilt) {
    move_eye(&left_eye, pan, tilt);
    move_eye(&right_eye, pan, tilt);
}

// Keep-alive thread - prevents USB suspend
void* keepalive_thread(void* arg) {
    eye_t* eye = (eye_t*)arg;
    struct v4l2_capability cap;

    while (running) {
        // Query capability to keep device active
        ioctl(eye->fd, VIDIOC_QUERYCAP, &cap);
        usleep(500000);  // Every 500ms
    }
    return NULL;
}

int open_camera(eye_t* eye, const char* device, const char* name) {
    eye->fd = open(device, O_RDWR);
    if (eye->fd < 0) {
        perror(device);
        return -1;
    }
    eye->name = name;
    eye->pan = 0;
    eye->tilt = 0;

    // Wake it up
    struct v4l2_capability cap;
    ioctl(eye->fd, VIDIOC_QUERYCAP, &cap);

    // Center it
    move_eye(eye, 0, 0);

    printf("Opened %s: %s\n", name, device);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <right_eye> <left_eye>\n", argv[0]);
        fprintf(stderr, "Example: %s /dev/video0 /dev/video2\n", argv[0]);
        return 1;
    }

    // Open cameras
    if (open_camera(&right_eye, argv[1], "RIGHT") < 0) return 1;
    if (open_camera(&left_eye, argv[2], "LEFT") < 0) return 1;

    // Start keepalive threads
    pthread_t left_thread, right_thread;
    pthread_create(&left_thread, NULL, keepalive_thread, &left_eye);
    pthread_create(&right_thread, NULL, keepalive_thread, &right_eye);

    // Setup terminal
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(cleanup);
    signal(SIGINT, sigint_handler);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    printf("\n=== STEREO VISION CONTROL ===\n");
    printf("Arrow keys: Pan/Tilt both eyes\n");
    printf("Space: Center\n");
    printf("Q: Quit\n\n");

    int32_t pan = 0, tilt = 0;
    char c;

    while (running) {
        if (read(STDIN_FILENO, &c, 1) == 1) {
            switch (c) {
                case 'q': case 'Q':
                    running = false;
                    break;

                case ' ':
                    pan = tilt = 0;
                    move_both(pan, tilt);
                    printf("CENTER\n");
                    break;

                case 27:  // Escape sequence
                    if (read(STDIN_FILENO, &c, 1) == 1 && c == '[') {
                        if (read(STDIN_FILENO, &c, 1) == 1) {
                            switch (c) {
                                case 'A': tilt += TILT_STEP; break;  // Up
                                case 'B': tilt -= TILT_STEP; break;  // Down
                                case 'C': pan += PAN_STEP; break;    // Right
                                case 'D': pan -= PAN_STEP; break;    // Left
                            }
                            move_both(pan, tilt);
                            printf("Pan: %+7d  Tilt: %+7d\n", pan, tilt);
                        }
                    }
                    break;
            }
        }
    }

    // Cleanup
    pthread_join(left_thread, NULL);
    pthread_join(right_thread, NULL);

    close(left_eye.fd);
    close(right_eye.fd);

    return 0;
}
