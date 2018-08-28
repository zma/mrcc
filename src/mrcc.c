// mrcc - A C Compiler system on MapReduce
// Zhiqiang Ma, https://www.ericzma.com

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mrcc.h"
#include "files.h"
#include "args.h"
#include "utils.h"
#include "cleanup.h"
#include "safeguard.h"
//#include "state.h"
#include "trace.h"
#include "traceenv.h"
#include "compile.h"


const char* mrcc_version = "0.1.0";

const char* rs_program_name = "mrcc";

static void show_version()
{
    printf(
"mrcc %s built at %s, %s\n"
"Copyright (C) 2009 by Zhiqiang Ma.\n"
"mrcc comes with ABSOLUTELY NO WARRANTY. mrcc is free software, and\n"
"you may use, modify and redistribute it under the terms of the GNU\n"
"General Public License version 2.\n"
"Please report bugs to eric.zq.ma [at] gmail.com.\n"
"\n"
        ,
        mrcc_version, __TIME__, __DATE__);
}
   

static void show_usage()
{
    printf(
"Usage:\n"
"   mrcc [COMPILER] [compile options] -o OBJECT -c SOURCE\n"
"   mrcc --help\n"
"\n"
"Options:\n"
"   COMPILER                   defaults to \"cc\"\n"
"   --help                     explain usage and exit\n"
"   --version                  show version and exit\n"
"\n"
/*
"Environment variables:\n"
"   See the manual page for a complete list.\n"
"   MRCC_VERBOSE=1           give debug messages\n"
"   MRCC_LOG                 send messages to file, not stderr\n"
"   MRCC_DIR                 directory for host list and locks\n"
"\n"
*/
"mrcc is a C Compiler system on MapReduce.\n"
"mrcc distributes compilation jobs across slave machines on MapReduce.\n"
"Jobs that cannot be distributed, such as linking or preprocessing\n"
"are run locally on master. mrcc should be used with make's -jN option\n"
"to execute in parallel on MapReduce.\n"
        );
}

static void show_help()
{
    show_version();
    show_usage();
}

int main(int argc, char* argv[])
{
    int status, sg_level;
    char** compiler_args = NULL; /* dynamically allocated */
    const char* compiler_name;
  
    int ret;

    client_catch_signals();
    atexit(cleanup_tempfiles);
    //atexit(remove_state_file);

    set_trace_from_env();
    note_called_time();
    trace_version();
    
    compiler_name = (char *) find_basename(argv[0]);

    /* Ignore SIGPIPE; we consistently check error codes and will
     * see the EPIPE. */
    ignore_sigpipe(1);

    sg_level = recursion_safeguard();

    rs_trace("compiler name is \"%s\"", compiler_name);

    if (!strcmp(compiler_name, "mrcc")) {
        // Either "mrcc -c hello.cpp" or "mrcc gcc -c hello.c"
        if (argc <= 1 || !strcmp(argv[1], "--help")) {
            show_help();
            ret = 0;
            goto out;
        }
        if (!strcmp(argv[1], "--version")) {
            show_version();
            ret = 0;
            goto out;
        }
        if ((ret = find_compiler(argv, &compiler_args)) != 0) {
            goto out;
        }
    }
    else {
        // NOT support masquerade by now
        printf("Sorry, we do not support masquerade by now.\n");
        ret = EXIT_MRCC_FAILED;
        goto out;
        
    }

    // Compile now
    ret = build_somewhere_timed(compiler_args, sg_level, &status);
    compiler_args = NULL; /* build_somewhere_timed already free'd it. */

out:
    return ret;
}

