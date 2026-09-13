/*
 * paths.c -- the XDG rules, in one place.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include "paths.h"

bool sw_data_dir(char *buf, size_t n)
{
    const char *data = getenv("XDG_DATA_HOME");
    if (data && *data == '/')
        return snprintf(buf, n, "%s/barnstormer", data) < (int)n;

    const char *home = getenv("HOME");
    if (!home || *home != '/')
        return false;
    return snprintf(buf, n, "%s/.local/share/barnstormer", home) < (int)n;
}

bool sw_data_path(char *buf, size_t n, const char *rel)
{
    char dir[512];
    if (!sw_data_dir(dir, sizeof(dir)))
        return false;
    return snprintf(buf, n, "%s/%s", dir, rel) < (int)n;
}

bool sw_make_dirs(const char *path)
{
    char tmp[512];
    if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp))
        return false;

    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
            return false;
        *p = '/';
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

bool sw_make_parent(const char *path)
{
    char tmp[512];
    if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp))
        return false;

    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp)
        return true;              /* a bare name, or the root: nothing to do */
    *slash = '\0';
    return sw_make_dirs(tmp);
}
