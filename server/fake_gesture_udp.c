// fake_gesture_udp.c
//
// Hardware-free test source for the BeagleY-AI gesture-control backend.
// Pretends to be the MediaPipe pose recogniser (m2demo): it PUSHES fake pose
// summaries to the Node server's UDP listener at ~1 Hz, cycling through
// "arm raised" (start), then idle, so you can verify the Single Shot / Sequence
// raised-arm start flow without a camera.
//
// Wire protocol (matches m2demo / gesture_server.js):
//   "pose <present> <arm_raised>"  where each field is 0/1, e.g. "pose 1 1"
//   (person visible, an arm raised) or "pose 0 0" (no person).
//
// Build:  gcc -O2 -Wall -Wextra -o fake_gesture_udp fake_gesture_udp.c
// Run:    ./fake_gesture_udp        (sends to 127.0.0.1:12345)
//         ./fake_gesture_udp 5000   (override the send interval, ms)

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int present;
    int arm_raised;
} Frame;

// The web UI holds a raised arm ~3s to confirm, so keep the arm up for several
// consecutive frames at the default 1000ms send interval, then drop it.
static const Frame kFrames[] = {
    {1, 1},  // person present, arm raised (start)
    {1, 1},
    {1, 1},
    {1, 1},
    {1, 0},  // arm lowered
    {0, 0},  // no person
    {0, 0},
    {0, 0},
};
static const int kNumFrames = (int)(sizeof(kFrames) / sizeof(kFrames[0]));

int main(int argc, char **argv) {
    const char *HOST = "127.0.0.1";
    const int PORT = 12345;
    long interval_ms = 1000;  // default: one new gesture per second
    if (argc > 1) {
        interval_ms = strtol(argv[1], NULL, 10);
        if (interval_ms <= 0) interval_ms = 1000;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(PORT);
    if (inet_pton(AF_INET, HOST, &dest.sin_addr) != 1) {
        fprintf(stderr, "inet_pton failed\n");
        return 1;
    }

    printf("Fake gesture source pushing to %s:%d every %ld ms\n",
           HOST, PORT, interval_ms);

    struct timespec ts;
    ts.tv_sec = interval_ms / 1000;
    ts.tv_nsec = (interval_ms % 1000) * 1000000L;

    int idx = 0;
    for (;;) {
        const Frame *f = &kFrames[idx];
        char msg[128];
        snprintf(msg, sizeof(msg), "pose %d %d", f->present, f->arm_raised);

        if (sendto(sock, msg, strlen(msg), 0,
                   (struct sockaddr *)&dest, sizeof(dest)) < 0) {
            perror("sendto");
        } else {
            printf("TX: \"%s\"\n", msg);
        }

        idx = (idx + 1) % kNumFrames;
        nanosleep(&ts, NULL);
    }

    close(sock);
    return 0;
}
