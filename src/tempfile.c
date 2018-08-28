// mrcc - A C Compiler system on MapReduce
// Zhiqiang Ma, https://www.ericzma.com

#include <stdarg.h>

#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <time.h>
#include <ctype.h>
#include <assert.h>
#include <fcntl.h>

#include <sys/fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>

#include <signal.h>

#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/poll.h>

#include "utils.h"
#include "trace.h"

#include "stringutils.h"
#include "tempfile.h"
#include "cleanup.h"

/* This function returns a directory-name, it does not end in a slash. */
int get_tmp_top(const char **p_ret)
{
    const char *d;

    d = getenv("TMPDIR");

    if (!d || d[0] == '\0') {
        *p_ret = "/tmp";
        return 0;
    } else {
        *p_ret = d;
        return 0;
    }
}



/**
 * Create a file inside the temporary directory and register it for
 * later cleanup, and return its name.
 *
 * The file will be reopened later, possibly in a child.  But we know
 * that it exists with appropriately tight permissions.
 **/
int make_tmpnam(const char *prefix, const char *suffix, char **name_ret)
{
    char *s = NULL;
    const char *tempdir;
    int ret;
    unsigned long random_bits;
    int fd;

    if ((ret = get_tmp_top(&tempdir)))
        return ret;

    if (access(tempdir, W_OK|X_OK) == -1) {
        rs_log_error("can't use TMPDIR \"%s\": %s", tempdir, strerror(errno));
        return EXIT_IO_ERROR;
    }

    random_bits = (unsigned long) getpid() << 16;

# if HAVE_GETTIMEOFDAY
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        random_bits ^= tv.tv_usec << 16;
        random_bits ^= tv.tv_sec;
    }
# else
    random_bits ^= time(NULL);
# endif

    do {
        free(s);

        if (asprintf(&s, "%s/%s_%08lx%s",
                     tempdir,
                     prefix,
                     random_bits & 0xffffffffUL,
                     suffix) == -1)
            return EXIT_OUT_OF_MEMORY;

        /* Note that if the name already exists as a symlink, this
         * open call will fail.
         *
         * The permissions are tight because nobody but this process
         * and our children should do anything with it. */
        fd = open(s, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd == -1) {
            /* try again */
            rs_trace("failed to create %s: %s", s, strerror(errno));
            random_bits += 7777; /* fairly prime */
            continue;
        }

        if (close(fd) == -1) {  /* huh? */
            rs_log_warning("failed to close %s: %s", s, strerror(errno));
            return EXIT_IO_ERROR;
        }

        break;
    } while (1);

    if ((ret = add_cleanup(s))) {
        /* bailing out */
        unlink(s);
        free(s);
        return ret;
    }

    *name_ret = s;
    return 0;
}

/**
 * Return a static string holding MRCC_DIR, or ~/.mrcc.
 * The directory is created if it does not exist.
 **/
int get_top_dir(char **path_ret)
{
    char *env;
    static char *cached;
    int ret;

    if (cached) {
        *path_ret = cached;
        return 0;
    }

    if ((env = getenv("MRCC_DIR"))) {
        if ((cached = strdup(env)) == NULL) {
            return EXIT_OUT_OF_MEMORY;
        } else {
            *path_ret = cached;
            return 0;
        }
    }

    if ((env = getenv("HOME")) == NULL) {
        rs_log_warning("HOME is not set; can't find mrcc directory");
        return EXIT_BAD_ARGUMENTS;
    }

    if (asprintf(path_ret, "%s/.mrcc", env) == -1) {
        rs_log_error("asprintf failed");
        return EXIT_OUT_OF_MEMORY;
    }

    ret = mrcc_mkdir(*path_ret);
    if (ret == 0)
        cached = *path_ret;
    return ret;
}


/**
 * Create the directory @p path.  If it already exists as a directory
 * we succeed.
 **/
int mrcc_mkdir(const char *path)
{
    if ((mkdir(path, 0777) == -1) && (errno != EEXIST)) {
        rs_log_error("mkdir '%s' failed: %s", path, strerror(errno));
        return EXIT_IO_ERROR;
    }

    return 0;
}

/**
 * Return a subdirectory of the MRCC_DIR of the given name, making
 * sure that the directory exists.
 **/
int get_subdir(const char *name, char **dir_ret)
{
    int ret;
    char *topdir;

    if ((ret = get_top_dir(&topdir)))
        return ret;

    if (asprintf(dir_ret, "%s/%s", topdir, name) == -1) {
        rs_log_error("asprintf failed");
        return EXIT_OUT_OF_MEMORY;
    }

    return mrcc_mkdir(*dir_ret);
}


int get_lock_dir(char **dir_ret)
{
    static char *cached;
    int ret;

    if (cached) {
        *dir_ret = cached;
        return 0;
    } else {
        ret = get_subdir("lock", dir_ret);
        if (ret == 0)
            cached = *dir_ret;
        return ret;
    }
}


int get_state_dir(char **dir_ret)
{
    static char *cached;
    int ret;

    if (cached) {
        *dir_ret = cached;
        return 0;
    } else {
        ret = get_subdir("state", dir_ret);
        if (ret == 0)
            cached = *dir_ret;
        return ret;
    }
}


