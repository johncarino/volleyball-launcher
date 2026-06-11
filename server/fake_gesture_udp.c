// fake_gesture_udp.c
//
// Hardware-free test source for the BeagleY-AI gesture-control backend.
// Pretends to be the MediaPipe recogniser (m2demo): it PUSHES fake gesture
// summaries to the Node server's UDP listener at ~10 Hz, cycling through a
// handful of named gestures so you can verify the web UI without a camera.
//
// Wire protocol (matches m2demo / gesture_server.js):
//   "gesture <count> <thumb> <index> <middle> <ring> <pinky> <NAME>"
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
    int count;
    int fingers[5];  // thumb, index, middle, ring, pinky
    const char *name;
} Gesture;

static const Gesture kGestures[] = {
    {0, {0, 0, 0, 0, 0}, "FIST"},
    {1, {0, 1, 0, 0, 0}, "POINT"},
    {2, {0, 1, 1, 0, 0}, "PEACE"},
    {1, {1, 0, 0, 0, 0}, "THUMBS_UP"},
    {4, {0, 1, 1, 1, 1}, "FOUR"},
    {5, {1, 1, 1, 1, 1}, "OPEN_PALM"},
    {2, {1, 0, 0, 0, 1}, "CALL_ME"},
    {0, {0, 0, 0, 0, 0}, "NONE"},
};
static const int kNumGestures = (int)(sizeof(kGestures) / sizeof(kGestures[0]));

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
        const Gesture *g = &kGestures[idx];
        char msg[128];
        snprintf(msg, sizeof(msg), "gesture %d %d %d %d %d %d %s",
                 g->count, g->fingers[0], g->fingers[1], g->fingers[2],
                 g->fingers[3], g->fingers[4], g->name);

        if (sendto(sock, msg, strlen(msg), 0,
                   (struct sockaddr *)&dest, sizeof(dest)) < 0) {
            perror("sendto");
        } else {
            printf("TX: \"%s\"\n", msg);
        }

        idx = (idx + 1) % kNumGestures;
        nanosleep(&ts, NULL);
    }

    close(sock);
    return 0;
}
