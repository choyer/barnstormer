/*
 * level.c -- level persistence.
 *
 * Loading and saving user-built levels is planned but not implemented; see
 * doc/ROADMAP.md for the design and doc/LEVEL_FORMAT.md for the file layout
 * these functions will read and write.  They are declared and stubbed now so
 * that the rest of the program can be written against the final interface.
 */
#include <errno.h>

#include "level.h"

int level_load(const char *path, level_t **out)
{
    (void)path; (void)out;
    errno = ENOSYS;
    return -1;
}

int level_save(const char *path, const level_t *lvl)
{
    (void)path; (void)lvl;
    errno = ENOSYS;
    return -1;
}

int level_free(level_t *lvl)
{
    (void)lvl;
    errno = ENOSYS;
    return -1;
}
