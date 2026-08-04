// Minimal runtime-leveled logging shared by the HAL and app C/C++ sources.
// Level is parsed once from the LAUNCHER_LOG env var (debug|info|warn|error),
// defaulting to info; LOG_DEBUG is silent unless LAUNCHER_LOG=debug. Errors and
// warnings go to stderr, info/debug to stdout.
#ifndef HAL_LOG_H
#define HAL_LOG_H

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

enum {
    LOG_LVL_ERROR = 0,
    LOG_LVL_WARN  = 1,
    LOG_LVL_INFO  = 2,
    LOG_LVL_DEBUG = 3
};

// Cached per translation unit; the environment is read on first use only.
static inline int log_active_level(void) {
    static int level = -1;
    if (level < 0) {
        const char *env = getenv("LAUNCHER_LOG");
        if (env == NULL || env[0] == '\0') level = LOG_LVL_INFO;
        else if (strcasecmp(env, "debug") == 0) level = LOG_LVL_DEBUG;
        else if (strcasecmp(env, "info") == 0) level = LOG_LVL_INFO;
        else if (strcasecmp(env, "warn") == 0 || strcasecmp(env, "warning") == 0) level = LOG_LVL_WARN;
        else if (strcasecmp(env, "error") == 0) level = LOG_LVL_ERROR;
        else level = LOG_LVL_INFO;
    }
    return level;
}

#define LOG_ERROR(...) do { if (log_active_level() >= LOG_LVL_ERROR) fprintf(stderr, __VA_ARGS__); } while (0)
#define LOG_WARN(...)  do { if (log_active_level() >= LOG_LVL_WARN)  fprintf(stderr, __VA_ARGS__); } while (0)
#define LOG_INFO(...)  do { if (log_active_level() >= LOG_LVL_INFO)  fprintf(stdout, __VA_ARGS__); } while (0)
#define LOG_DEBUG(...) do { if (log_active_level() >= LOG_LVL_DEBUG) fprintf(stdout, __VA_ARGS__); } while (0)

#endif  // HAL_LOG_H
