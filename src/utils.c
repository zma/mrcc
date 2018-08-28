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

#include "stringutils.h"
#include "cleanup.h"
#include "utils.h"
#include "trace.h"

/* Set the PATH environment variable to the indicated value. */
int set_path(const char *newpath)
{
    char *buf;

    if (asprintf(&buf, "PATH=%s", newpath) <= 0 || !buf) {
        rs_log_error("failed to allocate buffer for new PATH");
        return EXIT_OUT_OF_MEMORY;
    }
    rs_trace("setting %s", buf);
    if (putenv(buf) < 0) {
        rs_log_error("putenv PATH failed");
        return EXIT_FAILURE;
    }
    /* We must leave "buf" allocated. */
    return 0;
}



/**
 * Look up a boolean environment option, which must be either "0" or
 * "1".  The default, if it's not set or is empty, is @p default.
 **/
int getenv_bool(const char *name, int default_value)
{
    const char *e;

    e = getenv(name);
    if (!e || !*e)
        return default_value;
    if (!strcmp(e, "1"))
        return 1;
    else if (!strcmp(e, "0"))
        return 0;
    else
        return default_value;
}


/* Return the supplied path with the current-working directory prefixed (if
 * needed) and all "dir/.." references removed.  Supply path_len if you want
 * to use only a substring of the path string, otherwise make it 0. */
char *abspath(const char *path, int path_len)
{
 
/* XXX: Kind of kludgy, we should do dynamic allocation.  But this will do for
 * now. */
#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif

    static char buf[MAXPATHLEN];
    unsigned len;
    char *p, *slash;

    if (*path == '/')
        len = 0;
    else {
#ifdef HAVE_GETCWD
        getcwd(buf, sizeof buf);
#else
        // getwd is deprecated
        // getwd(buf);
        getcwd(buf, sizeof buf);
#endif
        len = strlen(buf);
        if (len >= sizeof buf) {
            rs_log_crit("getwd overflowed in abspath()");
        }
        buf[len++] = '/';
    }
    if (path_len <= 0)
        path_len = strlen(path);
    if (path_len >= 2 && *path == '.' && path[1] == '/') {
        path += 2;
        path_len -= 2;
    }
    if (len + (unsigned)path_len >= sizeof buf) {
        rs_log_error("path overflowed in abspath()");
        exit(EXIT_OUT_OF_MEMORY);
    }
    strncpy(buf + len, path, path_len);
    buf[len + path_len] = '\0';
    for (p = buf+len-(len > 0); (p = strstr(p, "/../")) != NULL; p = slash) {
        *p = '\0';
        if (!(slash = strrchr(buf, '/')))
            slash = p;
        strcpy(slash, p+3);
    }
    return buf;
}


void mrcc_exit(int exitcode)
{
    struct rusage self_ru, children_ru;

    if (getrusage(RUSAGE_SELF, &self_ru)) {
        rs_log_warning("getrusage(RUSAGE_SELF) failed: %s", strerror(errno));
        memset(&self_ru, 0, sizeof self_ru);
    }
    if (getrusage(RUSAGE_CHILDREN, &children_ru)) {
        rs_log_warning("getrusage(RUSAGE_CHILDREN) failed: %s", strerror(errno));
        memset(&children_ru, 0, sizeof children_ru);
    }

    /* NB fields must match up for microseconds */
    rs_log(RS_LOG_INFO,
          "exit: code %d; self: %d.%06d user %d.%06d sys; children: %d.%06d user %d.%06d sys",
           exitcode,
           (int) self_ru.ru_utime.tv_sec, (int) self_ru.ru_utime.tv_usec,
           (int) self_ru.ru_stime.tv_sec, (int) self_ru.ru_stime.tv_usec,
           (int) children_ru.ru_utime.tv_sec, (int) children_ru.ru_utime.tv_usec,
           (int) children_ru.ru_stime.tv_sec, (int)  children_ru.ru_stime.tv_usec);
    exit(exitcode);
}

/* Subtract the `struct timeval' values X and Y,
   storing the result in RESULT.
   Return 1 if the difference is negative, otherwise 0.
*/
int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y)
{
    /* Perform the carry for the later subtraction by updating Y. */
    if (x->tv_usec < y->tv_usec) {
        int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
        y->tv_usec -= 1000000 * nsec;
        y->tv_sec += nsec;
    }
    if (x->tv_usec - y->tv_usec > 1000000) {
        int nsec = (x->tv_usec - y->tv_usec) / 1000000;
        y->tv_usec += 1000000 * nsec;
        y->tv_sec -= nsec;
    }

    /* Compute the time remaining to wait.
       `tv_usec' is certainly positive. */
    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_usec = x->tv_usec - y->tv_usec;

    /* Return 1 if result is negative. */
    return x->tv_sec < y->tv_sec;
}


/**
 * For masquerade mode, change the path to remove the directory containing the
 * mrcc mask, so that invoking the same name will find the underlying
 * compiler instead.
 *
 * @param progname basename under which mrcc was introduced.  If we reached
 * this point, then it's the same as the name of the real compiler, e.g. "cc".
 *
 * @param did_masquerade specifies an integer that will be set to 1 if the
 * path was changed.
 *
 * @return 0 or standard error.
 **/
int support_masquerade(char *argv[], const char *progname, int *did_masquerade)
{
    const char *envpath, *findpath, *p, *n;
    char *buf;
    size_t len;
    size_t findlen;

    if (!(envpath = getenv("PATH")))
        /* strange but true*/
        return 0;
    // DEBUG:
    printf("envpath: %s", envpath);

    if (!(buf = (char*)malloc(strlen(envpath)+1+strlen(progname)+1))) {
        rs_log_error("failed to allocate buffer for new PATH");
        return EXIT_OUT_OF_MEMORY;
    }

    /* Filter PATH to contain only the part that is past our dir.
     * If we were called explicitly, find the named dir on the PATH. */
    if (progname != argv[0]) {
        findpath = abspath(argv[0], progname - argv[0] - 1);
        findlen = strlen(findpath);
    } else {
        findpath = NULL;
        findlen = 0;
    }

    for (n = p = envpath; *n; p = n) {
        /* Find the length of this component of the path */
        n = strchr(p, ':');
        if (n)
            len = n++ - p;
        else {
            len = strlen(p);
            n = p + len;
        }

        if (findpath) {
            /* Looking for a component in the path equal to findpath */

            /* FIXME: This won't catch paths that are in fact the same, but
             * that are not the same string.  This might happen if you have
             * multiple slashes, or dots, or symlinks... */
            if (len != findlen || strncmp(p, findpath, findlen) != 0)
                continue;
        } else {
            /* Looking for a component in the path containing a file
             * progname. */

            /* FIXME: This gets a false match if you have a subdirectory that
             * happens to be of the right name, e.g. /usr/bin/mrcc... */
            strncpy(buf, p, (size_t) len);
            sprintf(buf + len, "/%s", progname);
            if (access(buf, X_OK) != 0)
                continue;
        }
        /* Set p to the part of the path past our match. */
        p = n;
        break;
    }

    if (*p != '\0') {
        int ret = set_path(p);
        if (ret)
            return ret;
        *did_masquerade = 1;
    }
    else {
        rs_trace("not modifying PATH");
        *did_masquerade = 0;
    }

    free(buf);
    return 0;
}



/** Given a host with its feature fields set, set
 *  its protover appropriately. Return the protover,
 *  or -1 on error.
 */
int get_protover_from_features(enum compress compr,
                                   enum cpp_where cpp_where,
                                   enum protover *protover)
{
    *protover = -1;

    if (compr == MRCC_COMPRESS_NONE && cpp_where == MRCC_CPP_ON_CLIENT) {
        *protover = MRCC_VER_1;
    }

    if (compr == MRCC_COMPRESS_LZO1X && cpp_where == MRCC_CPP_ON_SERVER) {
        *protover = MRCC_VER_3;
    }

    if (compr == MRCC_COMPRESS_LZO1X && cpp_where == MRCC_CPP_ON_CLIENT) {
        *protover = MRCC_VER_2;
    }

    if (compr == MRCC_COMPRESS_NONE && cpp_where == MRCC_CPP_ON_SERVER) {
        rs_log_error("pump mode (',cpp') requires compression (',lzo')");
    }

    return *protover;
}


int new_pgrp(void)
{
    /* If we're a session group leader, then we are not able to call
     * setpgid().  However, setsid will implicitly have put us into a new
     * process group, so we don't have to do anything. */

    /* Does everyone have getpgrp()?  It's in POSIX.1.  We used to call
     * getpgid(0), but that is not available on BSD/OS. */
    if (getpgrp() == getpid()) {
        rs_trace("already a process group leader");
        return 0;
    }

    if (setpgid(0, 0) == 0) {
        rs_trace("entered process group");
        return 0;
    } else {
        rs_trace("setpgid(0, 0) failed: %s", strerror(errno));
        return EXIT_MRCC_FAILED;
    }
}


/**
 * Ignore hangup signal.
 *
 * This is only used in detached mode to make sure the daemon does not
 * quit when whoever started it closes their terminal.  In nondetached
 * mode, the signal is logged and causes an exit as normal.
 **/
void ignore_sighup(void)
{
    signal(SIGHUP, SIG_IGN);

    rs_trace("ignoring SIGHUP");
}


/**
 * Ignore or unignore SIGPIPE.
 *
 * The server and child ignore it, because mrcc code wants to see
 * EPIPE errors if something goes wrong.  However, for invoked
 * children it is set back to the default value, because they may not
 * handle the error properly.
 **/
int ignore_sigpipe(int val)
{
    if (signal(SIGPIPE, val ? SIG_IGN : SIG_DFL) == SIG_ERR) {
        rs_log_warning("signal(SIGPIPE, %s) failed: %s",
                     val ? "ignore" : "default",
                     strerror(errno));
        return EXIT_MRCC_FAILED;
    }
    return 0;
}


/* Define as the return type of signal handlers (`int' or `void'). */
#define RETSIGTYPE void

static RETSIGTYPE client_signalled (int whichsig)
{
    signal(whichsig, SIG_DFL);

#ifdef HAVE_STRSIGNAL
    rs_log_info("%s", strsignal(whichsig));
#else
    rs_log_info("terminated by signal %d", whichsig);
#endif

    cleanup_tempfiles_from_signal_handler();

    raise(whichsig);

}

void client_catch_signals(void)
{
    signal(SIGTERM, &client_signalled);
    signal(SIGINT, &client_signalled);
    signal(SIGHUP, &client_signalled);
}

