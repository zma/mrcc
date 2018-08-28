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
#include "exec.h"
#include "compile.h"
//#include "state.h"
//#include "lock.h"
#include "utils.h"
#include "args.h"
#include "tempfile.h"
#include "files.h"
#include "remote.h"
#include "stringutils.h"
#include "io.h"


struct hostdef mrcc_local = {
    MRCC_MODE_LOCAL,
    NULL,
    (char *) "localhost",
    0,
    NULL,
    1,                          /* host is_up */
    MAX_LOCAL_TASKS,            /* number of tasks */
    (char *)"localhost",        /* verbatim string */
    MRCC_CPP_ON_CLIENT,         /* where to cpp (ignored) */
    NULL
};

struct hostdef *hostdef_local = &mrcc_local;


/* Note that we use the _same_ lock file for
 * hostdef_local and hostdef_local_cpp,
 * so that they both use the same underlying lock.
 * This ensures that we respect the limits for
 * both "localslots" and "localslots_cpp".
 *
 * Extreme care with lock ordering is required in order to avoid
 * deadlocks.  In particular, the following invariants apply:
 *
 *  - Each mrcc process should hold no more than two locks at a time;
 *    one local lock, and one remote lock.
 *
 *  - When acquring more than one lock, a strict lock ordering discipline
 *    must be observed: the remote lock must be acquired first, before the
 *    local lock; and conversely the local lock must be released first,
 *    before the remote lock.
 */





/**
 * If the input filename is a plain source file rather than a
 * preprocessed source file, then preprocess it to a temporary file
 * and return the name in @p cpp_fname.
 *
 * The preprocessor may still be running when we return; you have to
 * wait for @p cpp_fid to exit before the output is complete.  This
 * allows us to overlap opening the TCP socket, which probably doesn't
 * use many cycles, with running the preprocessor.
 **/
int cpp_maybe(char **argv, char *input_fname, char **cpp_fname,
          pid_t *cpp_pid)
{
    char **cpp_argv;
    int ret;
    char *input_exten;
    const char *output_exten;

    *cpp_pid = 0;

    if (is_preprocessed(input_fname)) {
        /* TODO: Perhaps also consider the option that says not to use cpp.
         * Would anyone do that? */
        rs_trace("input is already preprocessed");

        /* already preprocessed, great. */
        if (!(*cpp_fname = strdup(input_fname))) {
            rs_log_error("couldn't duplicate string");
            return EXIT_OUT_OF_MEMORY;
        }
        return 0;
    }

    input_exten = find_extension(input_fname);
    output_exten = preproc_exten(input_exten);
    if ((ret = make_tmpnam("mrcc", output_exten, cpp_fname)))
        return ret;

    /* We strip the -o option and allow cpp to write to stdout, which is
     * caught in a file.  Sun cc doesn't understand -E -o, and gcc screws up
     * -MD -E -o.
     *
     * There is still a problem here with -MD -E -o, gcc writes dependencies
     * to a file determined by the source filename.  We could fix it by
     * generating a -MF option, but that would break compilation with older
     * versions of gcc.  This is only a problem for people who have the source
     * and objects in different directories, and who don't specify -MF.  They
     * can fix it by specifying -MF.  */

    if ((ret = strip_dasho(argv, &cpp_argv))
        || (ret = set_action_opt(cpp_argv, "-E")))
        return ret;

    /* FIXME: cpp_argv is leaked */

    return spawn_child(cpp_argv, cpp_pid,
                           "/dev/null", *cpp_fname, NULL);
}







/**
 * Invoke a compiler locally.  This is, obviously, the alternative to
 * compile_remote().
 *
 * The server does basically the same thing, but it doesn't call this
 * routine because it wants to overlap execution of the compiler with
 * copying the input from the network.
 *
 * This routine used to exec() the compiler in place of mrcc.  That
 * is slightly more efficient, because it avoids the need to create,
 * schedule, etc another process.  The problem is that in that case we
 * can't clean up our temporary files, and (not so important) we can't
 * log our resource usage.
 *
 * This is called with a lock on localhost already held.
 **/
static int compile_local(char *argv[], char *input_name)
{
    pid_t pid;
    int ret;
    int status;

    note_execution(hostdef_local, argv);
    // note_state(MRCC_PHASE_COMPILE, input_name, "localhost");

    /* We don't do any redirection of file descriptors when running locally,
     * so if for example cpp is being used in a pipeline we should be fine. */
    if ((ret = spawn_child(argv, &pid, NULL, NULL, NULL)) != 0)
        return ret;

    if ((ret = collect_child("cc", pid, &status, timeout_null_fd)))
        return ret;

    return critique_status(status, "compile", input_name,
                               hostdef_local, 1);
}



/**
 * Execute the commands in argv remotely or locally as appropriate.
 *
 * We may need to run cpp locally; we can do that in the background
 * while trying to open a remote connection.
 *
 * This function is slightly inefficient when it falls back to running
 * gcc locally, because cpp may be run twice.  Perhaps we could adjust
 * the command line to pass in the .i file.  On the other hand, if
 * something has gone wrong, we should probably take the most
 * conservative course and run the command unaltered.  It should not
 * be a big performance problem because this should occur only rarely.
 *
 * @param argv Command to execute.  Does not include 0='mrcc'.
 * Must be dynamically allocated.  This routine deallocates it.
 *
 * @param status On return, contains the waitstatus of the compiler or
 * preprocessor.  This function can succeed (in running the compiler) even if
 * the compiler itself fails.  If either the compiler or preprocessor fails,
 * @p status is guaranteed to hold a failure value.
 *
 * Implementation notes:
 *
 * This code might be simpler if we would only acquire one lock
 * at a time.  But we need to choose the server host in order
 * to determine whether it supports pump mode or not,
 * and choosing the server host requires acquiring its lock
 * (otherwise it might be busy when we we try to acquire it).
 * So if the server chosen is not localhost, we need to hold the
 * remote host lock while we're doing local preprocessing or include
 * scanning.  Since local preprocessing/include scanning requires
 * us to acquire the local cpu lock, that means we need to hold two
 * locks at one time.
 *
 * TODO: make pump mode a global flag, and drop support for
 * building with cpp mode on some hosts and not on others.
 * Then change the code so that we only choose the remote
 * host after local preprocessing/include scanning is finished
 * and the local cpu lock is released.
 */
static int build_somewhere(char *argv[], int sg_level, int *status)
{

/**
 * This boolean is true iff --scan-includes option is enabled.
 * If so, mrcc will just run the source file through the include server,
 * and print out the list of header files that might be #included,
 * rather than actually compiling the sources.
 */
    static int _scan_includes = 0;


    char *input_fname = NULL, *output_fname, *cpp_fname, *deps_fname = NULL;
    char **files;
    char **server_side_argv = NULL;
    int server_side_argv_deep_copied = 0;
    char *server_stderr_fname = NULL;
    int needs_dotd = 0;
    //int sets_dotd_target = 0;
    pid_t cpp_pid = 0;
    int cpu_lock_fd = -1, local_cpu_lock_fd = -1;
    int ret;
    int remote_ret = 0;
    struct hostdef *host = NULL;
    char *_discrepancy_filename = NULL;
    char **new_argv;

    if ((ret = expand_preprocessor_options(&argv)) != 0)
        goto clean_up;

    if ((ret = discrepancy_filename(&_discrepancy_filename)))
        goto clean_up;

    if (sg_level) /* Recursive mrcc - run locally, and skip all locking. */
        goto run_local;

    /* TODO: Perhaps tidy up these gotos. */

    /* FIXME: this may leak memory for argv. */

    ret = scan_args(argv, &input_fname, &output_fname, &new_argv);
    free_argv(argv);
    argv = new_argv;
    if (ret != 0) {
        /* we need to scan the arguments even if we already know it's
         * local, so that we can pick up mrcc client options. */
        goto lock_local;
    }

    if ((ret = make_tmpnam("mrcc_server_stderr", ".txt",
                               &server_stderr_fname))) {
        /* So we are failing locally to make a temp file to store the
         * server-side errors in; it's unlikely anything else will
         * work, but let's try the compilation locally.
         */
        goto fallback;
    }

    // begin to compile on MapReduce now
    
    /* Lock the local CPU, since we're going to be doing preprocessing
     * or include scanning. */
    /*
    if ((ret = lock_local_cpp(&local_cpu_lock_fd)) != 0) {
        goto fallback;
    }
    */

    if (_scan_includes) {
        ret = 0; /*approximate_includes(host, argv);*/
        goto unlock_and_clean_up;
    }

    if (1) {
        files = NULL;

        if ((ret = cpp_maybe(argv, input_fname, &cpp_fname, &cpp_pid) != 0))
            goto fallback;

        if ((ret = strip_local_args(argv, &server_side_argv)))
            goto fallback;
    }

    if ((ret = compile_remote(server_side_argv,
                                  input_fname,
                                  cpp_fname,
                                  files,
                                  output_fname,
                                  needs_dotd ? deps_fname : NULL,
                                  server_stderr_fname,
                                  cpp_pid, local_cpu_lock_fd,
                                  host, status)) != 0) {
        /* Returns zero if we successfully ran the compiler, even if
         * the compiler itself bombed out. */

        /* compile_remote() already unlocked local_cpu_lock_fd. */
        local_cpu_lock_fd = -1;

        goto fallback;
    }

    /* compile_remote() already unlocked local_cpu_lock_fd. */
    local_cpu_lock_fd = -1;
    /*
    mrcc_unlock(cpu_lock_fd);
    cpu_lock_fd = -1;
    */
    ret = critique_status(*status, "compile", input_fname, host, 1);
    if (ret == 0) {
        /* Try to copy the server-side errors on stderr.
         * If that fails, even though the compilation succeeded,
         * we haven't managed to give these errors to the user,
         * so we have to try again.
         * FIXME: Just like in the attempt to make a temporary file, this
         * is unlikely to fail, if it does it's unlikely any other
         * operation will work, and this makes the mistake of
         * blaming the server for what is (clearly?) a local failure.
         */
        if ((copy_file_to_fd(server_stderr_fname, STDERR_FILENO))) {
            rs_log_warning("Could not show server-side errors");
            goto fallback;
        }
        /* SUCCESS! */
        goto clean_up;
    }
    if (ret < 128) {
        /* Remote compile just failed, e.g. with syntax error.
           It may be that the remote compilation failed because
           the file has an error, or because we did something
           wrong (e.g. we did not send all the necessary files.)
           Retry locally. If the local compilation also fails,
           then we know it's the program that has the error,
           and it doesn't really matter that we recompile, because
           this is rare.
           If the local compilation succeeds, then we know it's our
           fault, and we should do something about it later.
           (Currently, we send email to an appropriate email address).
        */
        rs_log_warning("remote compilation of '%s' failed, retrying locally",
                     input_fname);
        remote_ret = ret;
        goto fallback;
    }

  fallback:

    if (cpu_lock_fd != -1) {
        // mrcc_unlock(cpu_lock_fd);
        cpu_lock_fd = -1;
    }
    if (local_cpu_lock_fd != -1) {
        // mrcc_unlock(local_cpu_lock_fd);
        local_cpu_lock_fd = -1;
    }

    if (!getenv_bool("MRCC_FALLBACK", 1)) {
        rs_log_warning("failed to distribute and fallbacks are disabled");
        /* Try copying any server-side error message to stderr;
         * If we fail the user will miss all the messages from the server; so
         * we pretend we failed remotely.
         */
        if ((copy_file_to_fd(server_stderr_fname, STDERR_FILENO))) {
                    rs_log_error("Could not print error messages from '%s'",
                       server_stderr_fname);
        }
        goto clean_up;
    }

    /* At this point, we can abandon the remote errors. */

    rs_log_warning("failed to distribute, running locally instead");

  lock_local:
    // lock_local(&cpu_lock_fd);

  run_local:
    /* Either compile locally, after remote failure, or simply do other cc tasks
       as assembling, linking, etc. */
    ret = compile_local(argv, input_fname);
//    if (remote_ret != 0 && remote_ret != ret) {
        /* Oops! it seems what we did remotely is not the same as what we did
          locally. We normally send email in such situations (if emailing is
          enabled), but we attempt an a time analysis of source files in order
          to avoid doing so in case source files we changed during the build.
        */
    /*
        (void) please_send_email_after_investigation(
            input_fname,
            deps_fname,
            discrepancy_filename);
    }
    */
  unlock_and_clean_up:
    if (cpu_lock_fd != -1) {
        // mrcc_unlock(cpu_lock_fd);
        cpu_lock_fd = -1; /* Not really needed, just for consistency. */
    }
    /* For the --scan_includes case. */
    if (local_cpu_lock_fd != -1) {
        // mrcc_unlock(local_cpu_lock_fd);
        local_cpu_lock_fd = -1; /* Not really needed, just for consistency. */
    }

  clean_up:
    free_argv(argv);
    if (server_side_argv_deep_copied) {
        if (server_side_argv != NULL) {
          free_argv(server_side_argv);
        }
    } else {
        free(server_side_argv);
    }
    free(_discrepancy_filename);
    return ret;
}


/*
 * argv must be dynamically allocated.
 * This routine will deallocate it.
 */
int build_somewhere_timed(char *argv[], int sg_level, int *status)
{
    struct timeval before, after, delta;
    int ret;

    if (gettimeofday(&before, NULL))
        rs_log_warning("gettimeofday failed");

    ret = build_somewhere(argv, sg_level, status);

    if (gettimeofday(&after, NULL)) {
        rs_log_warning("gettimeofday failed");
    } else {
        /* TODO: Show rate based on cpp size?  Is that meaningful? */
        timeval_subtract(&delta, &after, &before);

        rs_log(RS_LOG_INFO|RS_LOG_NONAME,
             "elapsed compilation time %ld.%06lds",
             delta.tv_sec, (long) delta.tv_usec);
    }

    return ret;
}


/**
 * Return in @param filename the name of the file we use as unary counter of
 * discrepancies (a compilation failing on the server, but succeeding
 * locally. This function may return NULL in @param filename if the name cannot
 * be determined.
 **/
int discrepancy_filename(char **filename)
{
    static const char *const include_server_port_suffix = "/socket";
    static const char *const discrepancy_suffix = "/discrepancy_counter";

    const char *include_server_port = getenv("INCLUDE_SERVER_PORT");
    *filename = NULL;
    if (include_server_port == NULL) {
        return 0;
    } else if (str_endswith(include_server_port_suffix,
                            include_server_port)) {
        /* We're going to make a longer string from include_server_port: one
         * that replaces include_server_port_suffix with discrepancy_suffix. */
        int delta = strlen(discrepancy_suffix) -
            strlen(include_server_port_suffix);
        assert (delta > 0);
        *filename = malloc(strlen(include_server_port) + 1 + delta);
        if (!*filename) {
            rs_log_error("failed to allocate space for filename");
            return EXIT_OUT_OF_MEMORY;
        }
        strcpy(*filename, include_server_port);
        int slash_pos = strlen(include_server_port)
                        - strlen(include_server_port_suffix);
        /* Because include_server_port_suffix is a suffix of include_server_port
         * we expect to find a '/' at slash_pos in filename. */
        assert((*filename)[slash_pos] == '/');
        (void) strcpy(*filename + slash_pos, discrepancy_suffix);
        return 0;
    } else
        return 0;
}





