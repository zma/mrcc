//mrcc-map - part of mrcc
//Zhiqiang Ma https://www.ericzma.com

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mrcc-map.h"
#include "args.h"
#include "traceenv.h"
#include "trace.h"
#include "files.h"
#include "netfsutils.h"
#include "cleanup.h"
#include "utils.h"
#include "args.h"


const char* mrcc_map_version = "0.1.0";

const char* rs_program_name = "mrcc-map";

static void map_show_version()
{
    printf(
"mrcc-map %s built at %s, %s\n"
"Copyright (C) 2009 by Zhiqiang Ma.\n"
"mrcc-map comes with ABSOLUTELY NO WARRANTY. mrcc-map is free software,\n"
"and you may use, modify and redistribute it under the terms of the GNU\n"
"General Public License version 2.\n"
"Please report bugs to eric.zq.ma [at] gmail.com.\n"
"\n"
        ,
        mrcc_map_version, __TIME__, __DATE__);
}

static void map_show_usage()
{
    printf(
"Usage:\n"
"mrcc-map is part of mrcc. mrcc is a C Compiler system on MapReduce.\n"
"mrcc distributes compilation jobs across slave machines on MapReduce.\n"
"Jobs that cannot be distributed, such as linking or preprocessing\n"
"are run locally on master. mrcc should be used with make's -jN option\n"
"to execute in parallel on MapReduce.\n"
        );
}

static void map_show_help()
{
    map_show_version();
    map_show_usage();
}

/*
 * for debugging only
 */
/*
static void log_int(char* msg, int ret)
{
    static FILE* fp = NULL;
    if (fp == NULL) {
        fp = fopen("/tmp/log_files","wb");
    }
    fprintf(fp, "%s: %d\n", msg, ret);
    return ;
}
*/

int main(int argc, char* argv[])
{
    int ret = 0;
    const char* compiler_name;
    char* cpp_fname;
    char** map_argv;
    char* map_argv_str;
    char* fs_cpp_fname;
    char* out_fname;
    char* fs_out_fname;

    // for debug only
    // int i;
    // FILE* log_file;
    // log_file = fopen("/tmp/mrcc-map.log", "w");
    // for (i = 0; i < argc; i++) {
    //     fprintf(log_file, "%s ", argv[i]);
    // }
    // fclose(log_file);
    // end debug

    if (argc <= 1 || !strcmp(argv[1], "--help")) {
        map_show_help();
        ret = 0;
        goto out;
    }
    else if (!strcmp(argv[1], "--version")) {
        map_show_version();
        ret = 0;
        goto out;
    }


    atexit(cleanup_tempfiles);

    set_trace_from_env();
    note_called_time();
    trace_version();

    cpp_fname = argv[1];
    rs_trace("cpp_fname is \"%s\"", cpp_fname);

    out_fname = argv[2];
    rs_trace("out_fname is \"%s\"", out_fname);
    map_argv = argv + 3;

    compiler_name = (char *) find_basename(map_argv[0]);
    rs_trace("compiler name is \"%s\"", compiler_name);
    
    // get cpp_fname from net fs
    rs_trace("get cpp from net fs: \"%s\"", cpp_fname);
    // error here on hadoop 0.20.2
    if ((fs_cpp_fname = name_local_to_fs(cpp_fname)) == NULL) {
        return EXIT_OUT_OF_MEMORY;
    }
    if (get_file_fs(fs_cpp_fname, cpp_fname) != 0) {
        rs_log_error("get cpp from net fs: \"%s\" failed", cpp_fname);
        ret =  EXIT_GET_CPP_FS_FAILED;
        goto out;
    } 
    else {
        ret = add_cleanup_fs(fs_cpp_fname);
    }
 
    free(fs_cpp_fname);
    fs_cpp_fname = NULL;

    // add clean up files - cpp_fname
    if ((ret = add_cleanup(cpp_fname)) != 0) {
        goto out;
    }
    rs_trace("add clean up file: \"%s\"", cpp_fname);

    // compile it now
    if ((map_argv_str = argv_tostr(map_argv)) == NULL) {
        return EXIT_OUT_OF_MEMORY;
    }
    rs_trace("compile on map: \"%s\"", map_argv_str);
    ret = system(map_argv_str);
    rs_trace("compile on map return %d ", ret);
    free(map_argv_str);
    if (ret != 0) {
        goto out;
    }

    // put output file to net fs
    if ((fs_out_fname = name_local_to_fs(out_fname)) == NULL) {
        return EXIT_OUT_OF_MEMORY;
    }
    rs_trace("put output file to net fs: \"%s\"", out_fname);
    if (put_file_fs(out_fname, fs_out_fname) != 0) {
        ret =  EXIT_GET_CPP_FS_FAILED;
        rs_log_error("put output file to  net fs: \"%s\" failed", out_fname);
        goto out;
    }
    free(fs_out_fname);
    fs_cpp_fname = NULL;
   
    // add clean up files - output_fname
    rs_trace("add clean up file out_fname: \"%s\"", out_fname);
    if ((ret = add_cleanup(out_fname)) != 0) {
        goto out;
    }

out:
    if (ret != 0)
        return EXIT_MAPPER_FAILED;
    return 0;
    // fclose(stdout);
    // exit(0);
}

