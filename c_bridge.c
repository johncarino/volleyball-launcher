/*
 * c_bridge.c  –  Motor-control UDP bridge
 *
 * Listens on 127.0.0.1:BRIDGE_PORT for ASCII command datagrams sent by the
 * Node.js web server and translates them into motor_control API calls.
 *
 * UDP wire protocol (one datagram per command, ASCII, no newline required):
 *   "SPEED <0-100>"   – set launch-wheel speed (percent)
 *   "ANGLE <0-90>"    – set elevation angle (degrees)
 *   "STOP"            – emergency-stop all motors
 *
 * Build (from the volleyball-launcher/ directory):
 *   gcc -Wall -o c_bridge c_bridge.c motor_control.c
 *
 * Run:
 *   ./c_bridge            # uses defaults: 127.0.0.1:12346
 *   BRIDGE_PORT=9999 ./c_bridge
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

/* POSIX socket headers */
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "motor_control.h"

/* ---- Configuration -------------------------------------------------------- */
#define DEFAULT_HOST  "127.0.0.1"
#define DEFAULT_PORT  12346
#define BUF_SIZE      256

/* ---- Globals -------------------------------------------------------------- */
static volatile int running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

/* ---- Command parser ------------------------------------------------------- */
/*
 * Parse and execute one null-terminated command string.
 * Returns 0 on success, -1 on parse error.
 */
static int dispatch_command(const char *cmd)
{
    int value;

    if (sscanf(cmd, "SPEED %d", &value) == 1) {
        printf("[bridge] SPEED %d\n", value);
        set_motor_speed(value);
        return 0;
    }

    if (sscanf(cmd, "ANGLE %d", &value) == 1) {
        printf("[bridge] ANGLE %d\n", value);
        set_motor_angle(value);
        return 0;
    }

    if (strncmp(cmd, "STOP", 4) == 0) {
        printf("[bridge] STOP\n");
        stop_motor();
        return 0;
    }

    fprintf(stderr, "[bridge] Unknown command: '%s'\n", cmd);
    return -1;
}

/* ---- Main ----------------------------------------------------------------- */
int main(void)
{
    /* Read optional environment overrides */
    const char *host_env = getenv("BRIDGE_HOST");
    const char *port_env = getenv("BRIDGE_PORT");

    const char *bind_host = host_env ? host_env : DEFAULT_HOST;
    int         bind_port = port_env ? atoi(port_env) : DEFAULT_PORT;

    /* Install signal handlers so we can clean up on Ctrl-C / SIGTERM */
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    /* Create UDP socket */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        fprintf(stderr, "[bridge] socket(): %s\n", strerror(errno));
        return 1;
    }

    /* Allow quick restart */
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Bind */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)bind_port);
    if (inet_pton(AF_INET, bind_host, &addr.sin_addr) != 1) {
        fprintf(stderr, "[bridge] Invalid host: %s\n", bind_host);
        close(sock);
        return 1;
    }

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[bridge] bind(%s:%d): %s\n",
                bind_host, bind_port, strerror(errno));
        close(sock);
        return 1;
    }

    printf("[bridge] Motor-control bridge listening on %s:%d\n",
           bind_host, bind_port);

    /* Receive loop */
    char buf[BUF_SIZE];
    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);

    while (running) {
        ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&sender, &sender_len);
        if (n < 0) {
            if (errno == EINTR) break;   /* interrupted by signal */
            fprintf(stderr, "[bridge] recvfrom(): %s\n", strerror(errno));
            break;
        }

        /* Null-terminate and strip trailing whitespace/newlines */
        buf[n] = '\0';
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' ||
                          buf[n-1] == ' '  || buf[n-1] == '\t')) {
            buf[--n] = '\0';
        }

        if (n == 0) continue;

        dispatch_command(buf);
    }

    printf("[bridge] Shutting down.\n");
    stop_motor();   /* safe-state on exit */
    close(sock);
    return 0;
}
