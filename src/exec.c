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
#include "utils.h"
#include "safeguard.h"
#include "args.h"

/**
 * Redirect a file descriptor into (or out of) a file.
 *
 * Used, for example, to catch compiler error messages into a
 * temporary file.
 **/
int redirect_fd(int fd, const char *fname, int mode)
{
    int newfd;

    /* ignore errors */
    close(fd);

    newfd = open(fname, mode, 0666);
    if (newfd == -1) {
        rs_log_crit("failed to reopen fd%d onto %s: %s",
                  fd, fname, strerror(errno));
        return EXIT_IO_ERROR;
    } else if (newfd != fd) {
        rs_log_crit("oops, reopened fd%d onto fd%d?", fd, newfd);
        return EXIT_IO_ERROR;
    }

    return 0;
}



/**
 * Redirect stdin/out/err.  Filenames may be NULL to leave them untouched.
 *
 * This is called when running a job remotely, but *not* when running
 * it locally, because people might e.g. want cpp to read from stdin.
 **/
int redirect_fds(const char *stdin_file, const char *stdout_file, const char *stderr_file)
{
    int ret;

    if (stdin_file)
        if ((ret = redirect_fd(STDIN_FILENO, stdin_file, O_RDONLY)))
            return ret;

    if (stdout_file) {
        if ((ret = redirect_fd(STDOUT_FILENO, stdout_file,
                                   O_WRONLY | O_CREAT | O_TRUNC)))
            return ret;
    }

    if (stderr_file) {
        /* Open in append mode, because the server will dump its own error
         * messages into the compiler's error file.  */
        if ((ret = redirect_fd(STDERR_FILENO, stderr_file,
                                   O_WRONLY | O_CREAT | O_APPEND)))
            return ret;
    }

    return 0;
}


/**
 * Replace this program with another in the same process.
 *
 * Does not return, either execs the compiler in place, or exits with
 * a message.
 **/
static void mrcc_execvp(char **argv)
{
    char *slash;

    execvp(argv[0], argv);

    /* If we're still running, the program was not found on the path.  One
     * thing that might have happened here is that the client sent an absolute
     * compiler path, but the compiler's located somewhere else on the server.
     * In the absence of anything better to do, we search the path for its
     * basename.
     *
     * Actually this code is called on both the client and server, which might
     * cause unintnded behaviour in contrived cases, like giving a full path
     * to a file that doesn't exist.  I don't think that's a problem. */

    slash = strrchr(argv[0], '/');
    if (slash)
        execvp(slash + 1, argv);

    /* shouldn't be reached */
    rs_log_error("failed to exec %s: %s", argv[0], strerror(errno));

    mrcc_exit(EXIT_COMPILER_MISSING); /* a generalization, i know */
}



/**
 * Called inside the newly-spawned child process to execute a command.
 * Either executes it, or returns an appropriate error.
 *
 * This routine also takes a lock on localhost so that it's counted
 * against the process load.  That lock will go away when the process
 * exits.
 *
 * In this current version locks are taken without regard to load limitation
 * on the current machine.  The main impact of this is that cpp running on
 * localhost will cause jobs to be preferentially distributed away from
 * localhost, but it should never cause the machine to deadlock waiting for
 * localhost slots.
 *
 * @param what Type of process to be run here (cpp, cc, ...)
 **/
static void inside_child(char **argv,
                             const char *stdin_file,
                             const char *stdout_file,
                             const char *stderr_file)
{
    int ret;

    if ((ret = ignore_sigpipe(0)))
        goto fail;              /* set handler back to default */

    /* Ignore failure */
    increment_safeguard();

    /* do this last, so that any errors from previous operations are
     * visible */
    if ((ret = redirect_fds(stdin_file, stdout_file, stderr_file)))
        goto fail;

    mrcc_execvp(argv);

    ret = EXIT_MRCC_FAILED;

    fail:
    mrcc_exit(ret);
}


/**
 * Run @p argv in a child asynchronously.
 *
 * stdin, stdout and stderr are redirected as shown, unless those
 * filenames are NULL.  In that case they are left alone.
 *
 * @warning When called on the daemon, where stdin/stdout may refer to random
 * network sockets, all of the standard file descriptors must be redirected!
 **/
int spawn_child(char **argv, pid_t *pidptr, const char *stdin_file, const char *stdout_file, const char *stderr_file)
{
    pid_t pid;

    trace_argv("forking to execute", argv);

    pid = fork();
    if (pid == -1) {
        rs_log_error("failed to fork: %s", strerror(errno));
        return EXIT_OUT_OF_MEMORY; /* probably */
    } else if (pid == 0) {
        /* If this is a remote compile,
     * put the child in a new group, so we can
     * kill it and all its descendents without killing mrccd
     * FIXME: if you kill mrccd while it's compiling, and
     * the compiler has an infinite loop bug, the new group
     * will run forever until you kill it.
     */
        if (stdout_file != NULL) {
            if (new_pgrp() != 0)
                rs_trace("Unable to start a new group\n");
        }
        inside_child(argv, stdin_file, stdout_file, stderr_file);
        /* !! NEVER RETURN FROM HERE !! */
    } else {
        *pidptr = pid;
        rs_trace("child started as pid%d", (int) pid);
        return 0;
    }

    // add return 0 for end
    return 0;
}


void note_execution(struct hostdef *host, char **argv)
{
    char *astr;

    astr = argv_tostr(argv);
    if (host) { 
        rs_log(RS_LOG_INFO|RS_LOG_NONAME, "exec on %s: %s",
            host->hostdef_string, astr);
    }
    else {
        rs_log(RS_LOG_INFO|RS_LOG_NONAME, "exec on somewhere: %s",
            astr);
    }    
    free(astr);
}


/* Define to 1 if you have the `waitpid' function. */
#define HAVE_WAITPID 1

static int sys_wait4(pid_t pid, int *status, int options, struct rusage *rusage)
{

    /* Prefer use waitpid to wait4 for non-blocking wait with WNOHANG option */
#ifdef HAVE_WAITPID
    /* Just doing getrusage(children) is not sufficient, because other
     * children may have exited previously. */
    memset(rusage, 0, sizeof *rusage);
    return waitpid(pid, status, options);
#elif HAVE_WAIT4
    return wait4(pid, status, options, rusage);
#else
#error Please port this
#endif
}

/*******************************************/
const int timeout_null_fd = -1;

/**
 * Blocking wait for a child to exit.  This is used when waiting for
 * cpp, gcc, etc.
 *
 * This is not used by the daemon-parent; it has its own
 * implementation in reap_kids().  They could be unified, but the
 * parent only waits when it thinks a child has exited; the child
 * waits all the time.
 **/
int collect_child(const char *what, pid_t pid, int *wait_status, int in_fd)
{
    static int job_lifetime = 0;
    struct rusage ru;
    pid_t ret_pid;

    int ret;
    int wait_timeout_sec;
    fd_set fds,readfds;

    wait_timeout_sec = job_lifetime;

    FD_ZERO(&readfds);
    if (in_fd != timeout_null_fd){
        FD_SET(in_fd,&readfds);
    }


    while (!job_lifetime || wait_timeout_sec-- >= 0) {

    /* If we're called with a socket, break out of the loop if the socket disconnects.
     * To do that, we need to block in select, not in sys_wait4.
     * (Only waitpid uses WNOHANG to mean don't block ever, so I've modified
     *  sys_wait4 above to preferentially call waitpid.)
     */
    int flags = (in_fd == timeout_null_fd) ? 0 : WNOHANG;
        ret_pid = sys_wait4(pid, wait_status, flags, &ru);

    if (ret_pid == -1) {
        if (errno == EINTR) {
            rs_trace("wait4 was interrupted; retrying");
        } else {
            rs_log_error("sys_wait4(pid=%d) borked: %s", (int) pid, strerror(errno));
            return EXIT_MRCC_FAILED;
        }
    } else if (ret_pid != 0) {
        /* This is not the main user-visible message; that comes from
         * critique_status(). */
        rs_trace("%s child %ld terminated with status %#x",
               what, (long) ret_pid, *wait_status);
        rs_log_info("%s times: user %ld.%06lds, system %ld.%06lds, "
                  "%ld minflt, %ld majflt",
                  what,
                  ru.ru_utime.tv_sec, (long) ru.ru_utime.tv_usec,
                  ru.ru_stime.tv_sec, (long) ru.ru_stime.tv_usec,
                  ru.ru_minflt, ru.ru_majflt);

        return 0;
    }

    /* check timeout */
    if (in_fd != timeout_null_fd){
            struct timeval timeout;

        /* If client disconnects, the socket will become readable,
         * and a read should return -1 and set errno to EPIPE.
         */
        fds = readfds;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
        ret = select(in_fd+1,&fds,NULL,NULL,&timeout);
        if (ret == 1) {
            char buf;
            int nread = read(in_fd, &buf, 1);
            if ((nread == -1) && (errno == EWOULDBLOCK)) {
                /* spurious wakeup, ignore */
                ;
            } else if (nread == 0) {
                rs_log_error("Client fd disconnected, killing job");
                /* If killpg fails, it might means the child process is not
                 * in a new group, so, just kill the child process */
                if (killpg(pid,SIGTERM)!=0)
                    kill(pid, SIGTERM);
                return EXIT_IO_ERROR;
            } else if (nread == 1) {
                rs_log_error("Bug!  Read from fd succeeded when checking whether client disconnected!");
            } else
                rs_log_error("Bug!  nread %d, errno %d checking whether client disconnected!", nread, errno);
        }
    } else
        poll(NULL, 0, 1000);
    }
    /* If timeout, also kill the child process */
    if (killpg(pid,SIGTERM) !=0 )
        kill(pid, SIGTERM);
    rs_log_error("Compilation takes too long, timeout.");

    return EXIT_TIMEOUT;
}

/**
 * Analyze and report to the user on a command's exit code.
 *
 * @param command short human-readable description of the command (perhaps
 * argv[0])
 *
 * @returns 0 if the command succeeded; 128+SIGNAL if it stopped on a
 * signal; otherwise the command's exit code.
 **/
int critique_status(int status,
                        const char *command,
                        const char *input_fname,
                        struct hostdef *host,
                        int verbose)
{
    // deleted
    return 0;
    
}


