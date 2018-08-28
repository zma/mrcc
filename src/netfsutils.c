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
#include "stringutils.h"
#include "trace.h"
#include "cleanup.h"

// net fs oporation command
const char* put_file_fs_cmd = "/lhome/mr/hadoop-0.20.2/bin/hadoop dfs -put";
const char* get_file_fs_cmd = "/lhome/mr/hadoop-0.20.2/bin/hadoop dfs -get";
const char* del_file_fs_cmd = "/lhome/mr/hadoop-0.20.2/bin/hadoop dfs -rmr";

// top dir of temp files in net fs
const char* fs_top_dir = "mrcc";

// output file suffix in net fs
const char* fs_out_file_suffix = ".o";

// out dir suffix in net fs
const char* fs_out_dir_suffix = ".odir";

// the prefix for noting the file is on net fs when clean up
const char* net_file_prefix_for_clean_up = "#";


/*
 * append out file suffix to cpp_fname
 * caller is responsible for free the return string
 */
char* name_local_cpp_to_local_outfile(char* cpp_fname)
{
    char* fsname = NULL;
    if (asprintf(&fsname, "%s%s", cpp_fname, fs_out_file_suffix)
            == -1) {
        return NULL;
    }
    return fsname;
}

/*
 * append out dir suffix to cpp_fname
 * caller is responsible for free the return string
 */
char* name_local_cpp_to_local_outdir(char* cpp_fname)
{
    char* fsname = NULL;
    if (asprintf(&fsname, "%s%s", cpp_fname, fs_out_dir_suffix)
            == -1) {
        return NULL;
    }
    return fsname;
}

/*
 * caller is responsible for free return char*
 */
char* name_local_to_fs(char* localname)
{
    char* fsname = NULL;
    if (asprintf(&fsname, "%s%s", fs_top_dir, localname) == -1) {
        return NULL;
    }
    return fsname;
}

/*
 * caller is responsible for free return char*
 */
char* name_fs_to_local(char* fsname)
{
    if (!str_startswith(fs_top_dir, fsname)) {
        return NULL;
    }
    return strdup(fsname + strlen(fs_top_dir));
}

/*
 * put file to net fs
 */

int put_file_fs(char* localsrc, char* dst)
{
    int ret;
    char* args = NULL;
    if (asprintf(&args, "%s %s %s", 
                put_file_fs_cmd, localsrc, dst) == -1) {
        return EXIT_OUT_OF_MEMORY;
    }
    ret = system(args);
    free(args);
    return ret;
}

/*
 * get file from net fs
 */
int get_file_fs(char* src, char* localdst)
{
    int ret;
    char* args = NULL;
    if (asprintf(&args, "%s %s %s", 
                get_file_fs_cmd, src, localdst) == -1) {
        return EXIT_OUT_OF_MEMORY;
    }
    ret = system(args);
    free(args);
    return ret;
}

/*
 * delete file from net fs
 */
int del_file_fs(char* fname)
{
    int ret;
    char* args = NULL;
    if (asprintf(&args, "%s %s", del_file_fs_cmd, fname) == -1) {
        return EXIT_OUT_OF_MEMORY;
    }
    ret = system(args);
    free(args);
    return ret;
}

/*
 * delete dir from net fs
 * we use del_file_fs instead, dir and file is no deference for net fs
 */
/*
int del_dir_fs(char* fname)
{
    int ret;
    char* args = NULL;
    if (asprintf(&args, "%s %s", del_dir_fs_cmd, fname) == -1) {
        return EXIT_OUT_OF_MEMORY;
    }
    ret = system(args);
    free(args);
    return ret;
}
*/

int is_cleanup_on_fs(char* fname)
{
    return (str_startswith(net_file_prefix_for_clean_up, fname));
}


int cleanup_file_fs(char* fname)
{
    return del_file_fs(fname + 1);
}


int add_cleanup_fs(char* fname)
{
    char* fs_fname = NULL;
    if(asprintf(&fs_fname, "%s%s", net_file_prefix_for_clean_up, fname) == -1) {
        rs_log_error("out of memory when add_cleanup_fs");
        return EXIT_OUT_OF_MEMORY;
    }
    return add_cleanup(fs_fname);
}

