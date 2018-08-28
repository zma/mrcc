
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "utils.h"
#include "trace.h"
#include "traceenv.h"

/**
 * Setup client error/trace output.
 *
 * Trace goes to the file specified by MRCC_LOG, if any.  Otherwise, it goes
 * to stderr, except that UNCACHED_ERR_FD can redirect it elsewhere, for use
 * under ccache.
 *
 * The exact setting of log level is a little strange, but for a good
 * reason: if you ask for verbose, you get everything.  Otherwise, if
 * you set a file, you get INFO and above.  Otherwise, you only get
 * WARNING messages.  In practice this seems to be a nice balance.
 **/
void set_trace_from_env(void)
{
    const char *logfile, *logfd_name;
    int fd;
    int failed_to_open_logfile = 0;
    int save_errno = 0;
    int level = RS_LOG_WARNING; /* by default, warnings only */

    /* let the decision on what to log rest on the loggers */
    /* the email-an-error functionality in emaillog.c depends on this */
    rs_trace_set_level(RS_LOG_DEBUG);

    if ((logfile = getenv("MRCC_LOG")) && logfile[0]) {
        fd = open(logfile, O_WRONLY|O_APPEND|O_CREAT, 0666);
        if (fd != -1) {
            /* asked for a file, and we can open that file:
               include info messages */
            level = RS_LOG_INFO;
        } else {
            /* asked for a file, can't use it; use stderr instead */
            fd = STDERR_FILENO;
            save_errno = errno;
            failed_to_open_logfile = 1;
        }
    } else {
        /* not asked for file */
        if ((logfd_name = getenv("UNCACHED_ERR_FD")) == NULL ||
            (fd = atoi(logfd_name)) == 0) {
            fd = STDERR_FILENO;
        }
    }

    if (getenv_bool("MRCC_VERBOSE", 0)) {
        level = RS_LOG_DEBUG;
    }

    rs_add_logger(rs_logger_file, level, NULL, fd);

    if (failed_to_open_logfile) {
        rs_log_error("failed to open logfile %s: %s",
                     logfile, strerror(save_errno));
    }
}
