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
#include "args.h"
#include "exec.h"
#include "remote.h"
//#include "state.h"
//#include "lock.h"
#include "netfsutils.h"
#include "stringutils.h"
#include "mrutils.h"
#include "compile.h"


static int wait_for_cpp(pid_t cpp_pid,
                            int *status,
                            const char *input_fname)
{
    int ret;

    if (cpp_pid) {
        // note_state(MRCC_PHASE_CPP, NULL, NULL);
        /* Wait for cpp to finish (if not already done), check the
         * result, then send the .i file */

        if ((ret = collect_child("cpp", cpp_pid, status, timeout_null_fd)))
            return ret;

        /* Although cpp failed, there is no need to try running the command
         * locally, because we'd presumably get the same result.  Therefore
         * critique the command and log a message and return an indication
         * that compilation is complete. */
        if (critique_status(*status, "cpp", input_fname, hostdef_local, 0))
            return 0;
    }
    return 0;
}

/*
 * put cpp file to net filesystem
 * return 0 if success
 */
int put_cpp_fs(char* cpp_fname)
{
    int ret;
    char *out = NULL;
    if ((out = name_local_to_fs(cpp_fname)) == NULL) {
        return EXIT_OUT_OF_MEMORY;
    }
    if (put_file_fs(cpp_fname, out) != 0) {
        ret = EXIT_PUT_CPP_FS_FAILED;
    }
    free(out);
    return ret;
}

/*
 * generate config file and put it to net fs
 * return 0 if success
 */
// this function is no use by now, always return 0
int put_config_fs(char** argv,
        char* input_fname,
        char* cpp_fname,
        char* output_fname)
{
    int ret = 0;

    return ret;
}

/* clean up temp files on net fs when failure occure
 */
// this function is no use by now, always return 0
/*
int clean_up_config_fs(char* cpp_fname)
{
   int ret = 0;
   return ret;
}
*/
/*
int clean_up_file_fs(char* local_fname)
{
    int ret;
    char* fsname = NULL;
    if ((fsname = name_local_to_fs(local_fname)) == NULL) {
        return EXIT_OUT_OF_MEMORY;
    }
    ret = del_file_fs(fsname);
    free(fsname);
    if (ret != 0) {
        rs_log_error("clean up net fs file for %s failed.", local_fname);
    }
    return ret;
}
*/
/*
int clean_up_dir_fs(char* local_fname)
{
    int ret;
    char* fsname = NULL;
    if ((fsname = name_local_to_fs(local_fname)) == NULL) {
        return EXIT_OUT_OF_MEMORY;
    }
    ret = del_file_fs(fsname);
    free(fsname);
    if (ret != 0) {
        rs_log_error("clean up net fs dir for %s failed.", local_fname);
    }
    return ret;
}
*/
/**
 * Put the cpp file and configuration files to the file system on net
 * When this function is called, the preprocessor has already been
 * started in the background. It may still be running or finisned. If
 * the preprocessor is still running, put the configuration to the fs
 * to overlap with the preprocessing.
 *
 * @param argv Compiler command to run
 * 
 * @param cpp_fname Filename of preprocessed source.  May not be complete yet,
 * depending on @p cpp_pid.
 *
 * @param output_fname File that the object code should be delivered to.
 *
 * @param cpp_pid If nonzero, the pid of the preprocessor.  Must be
 * allowed to complete before we send the input file.
 *
 * @param local_cpu_lock_fd If != -1, file descriptor for the lock file.
 * Should be != -1 iff (host->cpp_where != CPP_ON_SERVER).
 * If != -1, the lock must be held on entry to this function,
 * and THIS FUNCTION WILL RELEASE THE LOCK.
 *
 * Returns 0 on success, otherwise error.
 */

static int put_cpp_config_fs(char** argv,
                       char* input_fname,
                       char* cpp_fname,
                       char* output_fname,
                       pid_t cpp_pid,
                       int local_cpu_lock_fd,
                       struct hostdef *host /* no use by now */,
                       int* status)
{
    int ret = 0;
 
    if ((ret = wait_for_cpp(cpp_pid, status, input_fname)))
        goto out;

    /* We are done with local preprocessing.  Unlock to allow someone
     * else to start preprocessing. */
    if (local_cpu_lock_fd != -1) {
        //mrcc_unlock(local_cpu_lock_fd);
        local_cpu_lock_fd = -1;
    }
    if (*status != 0)
        goto out;
   
    if ((ret = put_cpp_fs(cpp_fname)) != 0) {
        rs_log_error("put cpp file \"%s\" to net fs failed", cpp_fname);
        goto out;
    }

    rs_trace("master finished sending cpp to net fs");

    /* no use now
    if ((ret = put_config_fs(argv, input_fname, cpp_fname, output_fname)) != 0) {
        rs_log_error("put config file for \"%s\" to net fs failed", cpp_fname);
        goto out;
    }
    */

    return ret;

out:
    if (local_cpu_lock_fd != -1) {
        // mrcc_unlock(local_cpu_lock_fd);
        local_cpu_lock_fd = -1; /* Not really needed; just for consistency. */
    }
    /* we cleanup them at atexit
    // cleanup temp files on net fs
    if (ret == EXIT_PUT_CONFIG_FS_FAILED) {
        rs_log_info("clean up config & cpp file for \"%s\" on net fs", cpp_fname);
        clean_up_config_fs(cpp_fname);
        clean_up_file_fs(cpp_fname);
    }
    else if (ret == EXIT_PUT_CPP_FS_FAILED) {
        rs_log_info("clean up cpp file  \"%s\" on net fs", cpp_fname);
        clean_up_file_fs(cpp_fname);
    }
    */

    return ret;
}

/*
 * call the mapper with a string argv
 * argv[0] is the cpp_fname
 * argv[1] ... is the running argv
 * source and object is replaced for the remote compilation
 * MapReduce will control the running of the job
 */
static int call_mapper(char** argv, char* input_fname, char* cpp_fname, char* output_fname) 
{
    int ret = EXIT_CALL_MAPPER_FAILED;
    char** new_argv = NULL;
    char* new_output_fname = NULL;
    char* str_argv = NULL;
    int i = 0;
    int argc = 0;

    if (copy_argv(argv, &new_argv, 0) != 0) {
        return EXIT_OUT_OF_MEMORY;
    }
    if ((new_output_fname = name_local_cpp_to_local_outfile(cpp_fname)) == NULL) {
        return EXIT_OUT_OF_MEMORY;
    }

    argc = argv_len(new_argv);
    for (i = 0; i < argc; i++) {
        if (str_equal(new_argv[i], input_fname)) {
            free(new_argv[i]);
            new_argv[i] = strdup(cpp_fname);
        }
        else if (str_equal(new_argv[i], output_fname)) {
            free(new_argv[i]);
            new_argv[i] = strdup(new_output_fname);
        }
    }

    if ((str_argv = argv_tostr(new_argv)) == NULL) {
        return EXIT_OUT_OF_MEMORY;
    }
    free_argv(new_argv);

    ret = mr_exec(str_argv, cpp_fname, new_output_fname);

    free(str_argv);
    free(new_output_fname);
    
    return ret;
}

/*
 * get the result from net fs and do cleanup at the same time
 * get the output file from network and put it to the right place
 * and do the net fs cleanup works at the same time
 */
static int get_result_fs(char* cpp_fname, char* output_fname) 
{
    int ret;
    char* out_fname = NULL;
    char* fsname = NULL;
    if ((out_fname = name_local_cpp_to_local_outfile(cpp_fname)) == NULL) {
        return EXIT_OUT_OF_MEMORY;
    }
    if ((fsname = name_local_to_fs(out_fname)) == NULL) {
        return EXIT_OUT_OF_MEMORY;
    }
    free(out_fname);
    out_fname = NULL;
    // get output file from net fs
    ret = get_file_fs(fsname, output_fname);
    if (ret == 0) {
        ret = add_cleanup_fs(fsname);
    }
    free(fsname);
    fsname = NULL;

    /* we cleanup them at atexit
    // clean up config files, no use by now
    ret = clean_up_config_fs(cpp_fname) || ret;
    // clean up out file
    ret = clean_up_file_fs(out_fname) || ret;
    free(out_fname);
    out_fname = NULL;
    // clean up cpp file
    ret = clean_up_file_fs(cpp_fname) || ret;
    // clean up output dir
    ret = clean_up_outdir_fs(cpp_fname) || ret;
    */

    return 0;
}
/*
int clean_up_outdir_fs(char* cpp_fname)
{
    int ret = 0;
    char* out_dir;
    if ((out_dir = name_local_cpp_to_local_outdir(cpp_fname)) == NULL) {
        return EXIT_OUT_OF_MEMORY;
    }
    ret = clean_up_file_fs(out_dir);
    free(out_dir);

    return ret;
}
*/
/**
 * Pass a compilation across the network.
 *
 * When this function is called, the preprocessor has already been
 * started in the background.  It may have already completed, or it
 * may still be running.  The goal is that preprocessing will overlap
 * with setting up the network connection, which may take some time
 * but little CPU.
 *
 * If this function fails, compilation will be retried on the local
 * machine.
 *
 * @param argv Compiler command to run.
 *
 * @param cpp_fname Filename of preprocessed source.  May not be complete yet,
 * depending on @p cpp_pid.
 *
 * @param files If we are doing preprocessing on the server, the names of
 * all the files needed; otherwise, NULL.
 *
 * @param output_fname File that the object code should be delivered to.
 *
 * @param cpp_pid If nonzero, the pid of the preprocessor.  Must be
 * allowed to complete before we send the input file.
 *
 * @param local_cpu_lock_fd If != -1, file descriptor for the lock file.
 * Should be != -1 iff (host->cpp_where != CPP_ON_SERVER).
 * If != -1, the lock must be held on entry to this function,
 * and THIS FUNCTION WILL RELEASE THE LOCK.
 *
 * @param host Definition of host to send this job to.
 *
 * @param status on return contains the wait-status of the remote
 * compiler.
 *
 * Returns 0 on success, otherwise error.  Returning nonzero does not
 * necessarily imply the remote compiler itself succeeded, only that
 * there were no communications problems.
 */
int compile_remote(char **argv,
                       char *input_fname,
                       char *cpp_fname,
                       char **files, /* no use */
                       char *output_fname,
                       char *deps_fname, /* no use */
                       char *server_stderr_fname, /* no use by now */
                       pid_t cpp_pid,
                       int local_cpu_lock_fd,
                       struct hostdef *host,
                       int *status)
{
    int ret = 0;
    struct timeval before;

    if (gettimeofday(&before, NULL))
        rs_log_warning("gettimeofday failed");

    note_execution(host, argv);
    // note_state(PHASE_CONNECT, input_fname, host->hostname);
    
    // copy the preprocessed file to network and put the configuration files
    // when we wait for the cpp to finish if it has not finished
    note_info_time("begin put_cpp_config_fs");
    if (put_cpp_config_fs(argv, input_fname, cpp_fname, output_fname,
            cpp_pid, local_cpu_lock_fd, host, status) != 0) {
        rs_log_error("put_cpp_config_fs failed!"); 
        ret = -1;
        goto out;
    }
    note_info_time("finish put_cpp_config_fs");
    // call the mapper
    note_info_time("begin call_mapper");
    if (call_mapper(argv, input_fname, cpp_fname, output_fname) != 0) {
        rs_log_error("call_mapper failed!");
        ret = -1;
        goto out;
    }
    note_info_time("finish call_mapper");
 
    // get the output file from network and put it to the right place
    // and do the net fs cleanup works at the same time
    note_info_time("begin get_result_fs");
    if (get_result_fs(cpp_fname, output_fname) != 0) {
        rs_log_error("get_result_fs failed!");
        ret = -1;
        goto out;
    }
    note_info_time("finish get_result-fs");

out:
    return ret;
}


