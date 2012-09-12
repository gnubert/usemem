#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <limits.h>
#include <float.h>
#include <errno.h>

#ifdef WIN32
#include <windows.h>
#endif

/* 
 * Copyright 2010 Ludwig Ortmann <ludwig@spline.inf.fu-berlin.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

# define VERSION "1.2.1"

# define USAGE "\
Usage: %s command [options] [space[k|K|m|M|g|G] [time[m|h|d|w]]]\n\
\n\
Commands:\n\
    zero            fill memory with zeros\n\
    random          fill memory with random bytes\n\
    help            print this help\n\
    version         print version\n\
\n\
Options:\n\
    -d, --dense     completely fill memory with random bytes\n\
    -q, --quiet     keep it down, will you?\n\
\n\
Not specifying time or space will result in usemem taking all\n\
available space and sleeping forever respectively.\n"

enum modes { ZEROES = 0x001, RANDOM = 0x010, DENSE = 0x100 };

void *mem;
static int verbose;

/*
 * use sysconf to get the size of one page
 * returns -1 on failure
 */
long get_pagesize()
{
    long pagesize;

#ifdef WIN32
    SYSTEM_INFO siSysInfo;
    GetSystemInfo(&siSysInfo);

    pagesize = siSysInfo.dwPageSize;
    return pagesize;
#else
    if ((pagesize = sysconf(_SC_PAGESIZE)) != -1)
        return pagesize;
    if ((pagesize = sysconf(_SC_PAGE_SIZE)) != -1)
        return  pagesize;
    return -1;
#endif
}

/* allocate memory */
size_t alloc(size_t amount)
{
    if (amount == 0) {
        /* grow until no more memory can be acquired */
        long chunksize;
        char *nmem;

        /* if page size is not available, allocate kilobyte wise */
        if ((chunksize = get_pagesize()) == -1)
            chunksize = 1024;

        nmem = malloc(amount);
        do {
            amount += chunksize;
            mem = nmem;
            nmem = realloc(mem, amount);
        } while (nmem != NULL);
        amount -= chunksize; /* compensate for the last iteration */
        if (verbose != 0)
            fprintf(stderr, "allocated %lu bytes of memory\n",
                (unsigned long)amount);
    } else {
        /* acquire one big lump */
        if (((mem = malloc(amount)) == NULL) && (amount > 0)) {
            fprintf(stderr, "not enough memory\n");
            exit(EXIT_FAILURE);
        }
    }
    return amount;
}

/*
 * Allocate and fill memory.
 * If amount == 0 acquire as much memory as possible.
 * If mode & ZEROES, fill with zeros,
 *         & RANDOM  fill with randomness,
 *         & DENSE   fill every byte
 */
void usemem(int mode, size_t amount)
{
#define size (sizeof(unsigned int))
    unsigned int *pos, *end;
    long stepping;
    long pagesize;

    /* allocate memory */
    amount = alloc(amount);

    /* borders of the allocated memory */
    pos = (unsigned int*)mem;
    end = (unsigned int*)((size_t)mem + amount);

    /* if in dense mode or pagesize not available,
     * set pagesize to sizeof(unsigned int) */
    if ( mode & DENSE || (pagesize = get_pagesize()) == -1 )
        pagesize = (long)size;
    /* set stepping accordingly */
    stepping = pagesize / size;

    /* fill memory */
    if (mode & ZEROES) {
        if (pagesize != (long)size)
            for (; pos < end; pos += stepping)
                *pos = 0;
        else /* memset is quicker */
            memset(mem, 0, amount);
    } else {
        /* the stdlib implementation of rand() */
        struct timeval t1;
        unsigned long next;

        gettimeofday(&t1, NULL);
        next = (t1.tv_usec * t1.tv_sec);

        for (; pos < end; pos += stepping) {
            next = next * 1103515245 + 12345;
            *pos = (unsigned int)(next%UINT_MAX);
        }
    }
#undef size
}

int time_scale(char e)
{
    int scale;
    switch (e) {
        case 'm':
            scale = 60;
            break;
        case 'h':
            scale = 60 * 60;
            break;
        case 'd':
            scale = 60 * 60 * 24;
            break;
        case 'w':
            scale = 60 * 60 * 24 * 7;
            break;
        default:
            if (e != '\0') {
                fprintf(stderr, "unknown modifier for time: %c\n", e);
                exit(EXIT_FAILURE);
            }
            scale = 1;
            break;
    }
    return scale;
}

int space_scale(char e)
{
    int scale;
    switch (e) {
        case 'g':
            scale = 1000 * 1000 * 1000;
            break;
        case 'G':
            scale = 1024 * 1024 * 1024;
            break;
        case 'm':
            scale = 1000 * 1000;
            break;
        case 'M':
            scale = 1024 * 1024;
            break;
        case 'k':
            scale = 1000;
            break;
        case 'K':
            scale = 1024;
            break;
        default:
            if (e != '\0') {
                fprintf(stderr, "unknown modifier for space: %c\n", e);
                exit(EXIT_FAILURE);
            }
            scale = 1;
            break;
    }
    return scale;
}

/* parse string, check for errors */
void set_coeff_scale(char *str, double *coefficient, int *scale, char *name)
{
    char *e;

    errno = 0;
    *coefficient = strtod(str, &e);
    if (errno == ERANGE) {
        perror("error parsing value");
        exit(EXIT_FAILURE);
    }

    if (strncmp(name, "space", 6) == 0)
        *scale = space_scale(*e);
    else
        *scale = time_scale(*e);

    if (*coefficient > (DBL_MAX / *scale)) {
        fprintf(stderr, "value for %s is too large to be scaled with %c\n", name, *e);
        exit(EXIT_FAILURE);
    }
}

size_t parse_space(char *space_str)
{
    int scale;
    double coefficient;
    size_t space;

    if (space_str == NULL)
        return (size_t)0;

    set_coeff_scale(space_str, &coefficient, &scale, "space");
    space = (size_t)(coefficient * scale);

    /* check if value was too large for size_t */
    if ((size_t)-1 < (coefficient * scale)) {
        fprintf(stderr, "value for space is too large.\n");
        fprintf(stderr, "maximum is %lu bytes.\n",
                (unsigned long)(size_t)-1);
        exit(EXIT_FAILURE);
    }

    return space;
}

unsigned int parse_time(char *time_str)
{ 
    int scale;
    double coefficient;
    unsigned int time;

    if (time_str == NULL)
        return (unsigned int)0;

    set_coeff_scale(time_str, &coefficient, &scale, "time");
    time = (unsigned int)(coefficient * scale);

    /* check if value was too large for unsigned int */
    if (UINT_MAX < (coefficient * scale)) {
        fprintf(stderr, "value for time is too large.\n");
        fprintf(stderr, "maximum is %u seconds.\n", UINT_MAX);
        exit(EXIT_FAILURE);
    }

    return time;
}


/* parse arguments,
 * print help/version
 * OR set mode, time, space */
void set_mode_time_space(int argc, char **argv, int *mode, unsigned int *time, size_t *space)
{
    char *progname;
    size_t len;
    progname = *argv;

    /* sanity check */
    if (argc <= 1) {
        fprintf(stderr, "Invalid invocation.\nTry '%s help' for help.\n", *argv);
        exit(EXIT_FAILURE);
    }

    /* parse command */
    argv++;
    argc--;
    while (*(*argv) == '-')
        *argv = *argv+1;
    len = strlen(*argv);
    if (len == 0)
        len = 1; /* work around strncmp matching empty string below */
    if (strncmp(*argv, "zero", len) == 0 ) {
        *mode = ZEROES;
    } else if (strncmp(*argv, "random", len) == 0 ) {
        *mode = RANDOM;
    } else if (strncmp(*argv, "help", len) == 0 ) {
        printf(USAGE, progname);
        exit(EXIT_SUCCESS);
    } else if (strncmp(*argv, "version", len) == 0 ) {
        printf("usemem version %s\n", VERSION);
        exit(EXIT_SUCCESS);
    } else {
        fprintf(stderr, "unknown command: '%s'\nTry '%s help' for help.\n",
                *argv, progname);
        exit(EXIT_FAILURE);
    }
    argv++; argc--;

    /* parse options */
    for ( ; argc > 0 ; argv++, argc--) {
        if (**argv != '-')
            break;
        while (*(*argv) == '-')
            *argv = *argv+1;
        len = strlen(*argv);
        if (len == 0)
            len = 1; /* work around strncmp matching empty string below */
        if (strncmp(*argv, "dense", len) == 0) {
            *mode |= DENSE;
        } else if (strncmp(*argv, "quiet", len) == 0 ) {
            verbose = 0;
        } else {
            fprintf(stderr, "unknown option '%s'\nTry '%s help' for help.\n",
                    *argv+1, progname);
            exit(EXIT_FAILURE);
        }
    }

    if (argc-- > 0)
        *space = parse_space(*(argv++));
    if (argc-- > 0)
        *time = parse_time(*(argv++));
    if (argc > 0)
        fprintf(stderr, "ignoring trailing arguments.\n");

}

int main(int argc, char **argv)
{
    int mode;
    unsigned int time;
    size_t space;

    space = 0;     /* fill */
    mode = ZEROES;  /* with 0s */
    time = 0;       /* hold indefinitely */
    verbose = 1;

    set_mode_time_space(argc, argv, &mode, &time, &space);

    /* try to acquire and fill memory */
    if (verbose != 0) {
        if (space > 0) {
            fprintf(stderr, "filling %lu byte%s of memory\n", 
                    (unsigned long)space, (space == 1) ? "" : "s");
        } else {
            fprintf(stderr, "filling up memory\n");
        }
    }
    usemem(mode, space);

    /* hold memory */
    if (time > 0) {
        if (verbose != 0)
            fprintf(stderr, "holding memory for %u second%s\n",
                time, (time == 1) ? "" : "s");
#ifdef WIN32
        Sleep(time);
#else
        sleep(time);
#endif
    } else {
        if (verbose != 0)
            fprintf(stderr, "holding memory indefinitely\n");
#ifdef WIN32
        Sleep(INFINITE);
#else
        pause();
#endif
    }

    free(mem);

    return EXIT_SUCCESS;
}
