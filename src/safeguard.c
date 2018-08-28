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

#include "trace.h"

/**
 * @file
 * @brief Protect against unbounded recursion.
 *
 * It would be fairly easy for somebody to get confused in masquerade mode and
 * try to get mrcc to invoke itself in a loop.  We can't always work out the
 * right thing to do but we can at least flag an error.
 *
 * This environment variable is set to guard against mrcc accidentally
 * recursively invoking itself, thinking it's the real compiler.
 **/

static int safeguard_level;

int recursion_safeguard(void)
{
    static const char safeguard_name[] = "MRCC_SAFEGUARD";

    char *env = getenv(safeguard_name);

    if (env) {
        rs_trace("safeguard: %s", env);
        if (!(safeguard_level = atoi(env)))
            safeguard_level = 1;
    }
    else
        safeguard_level = 0;
    rs_trace("safeguard level=%d", safeguard_level);

    return safeguard_level;
}


int increment_safeguard(void)
{
    static char safeguard_set[] = "MRCC_SAFEGUARD=1";

    if (safeguard_level > 0)
    safeguard_set[sizeof safeguard_set-2] = safeguard_level+'1';
    rs_trace("setting safeguard: %s", safeguard_set);
    if ((putenv(strdup(safeguard_set)) == -1)) {
        rs_log_error("putenv failed");
        /* and continue */
    }

    return 0;
}




