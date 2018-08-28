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
#include "args.h"
#include "netfsutils.h"
#include "trace.h"


// MapReduce operation command
const char* mr_exec_cmd_prefix = "/lhome/mr/hadoop-0.20.2/bin/hadoop jar /lhome/mr/hadoop-0.20.2/contrib/streaming/hadoop-0.20.2-streaming.jar -mapper ";
const char* mr_exec_cmd_mapper = "/usr/bin/mrcc-map ";
const char* mr_exec_cmd_parameter = "-numReduceTasks 0 -input null -output ";

int mr_exec(char* argv, char* cpp_fname, char* out_fname)
{
    int ret;
    char* out_dir = NULL;
    char* fs_out_dir = NULL;
    char* mr_argv = NULL;

    if ((out_dir = name_local_cpp_to_local_outdir(cpp_fname)) == NULL) {
        return EXIT_OUT_OF_MEMORY;
    }
    if ((fs_out_dir = name_local_to_fs(out_dir)) == NULL) {
        return EXIT_OUT_OF_MEMORY;
    }
    free(out_dir);

    if (asprintf(&mr_argv, "%s \"%s %s %s %s\" %s %s",
                    mr_exec_cmd_prefix,
                    mr_exec_cmd_mapper, cpp_fname, out_fname, argv,
                    mr_exec_cmd_parameter,
                    fs_out_dir) == -1) {
        return EXIT_OUT_OF_MEMORY;
    }
    rs_log_info("mr_exec: %s", mr_argv);
    ret = system(mr_argv);
    ret = add_cleanup_fs(fs_out_dir) || ret;
    free(fs_out_dir);
    free(mr_argv);

    return ret;
}


