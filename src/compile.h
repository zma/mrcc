// mrcc - A C Compiler system on MapReduce
// Zhiqiang Ma, https://www.ericzma.com

#ifndef _HEADER_COMPILE_H
# define _HEADER_COMPILE_H

// maximum local compile and cpp taks number
#define MAX_LOCAL_TASKS 64
#define MAX_LOCAL_CPP_TASKS 64

extern struct hostdef *hostdef_local;

int build_somewhere_timed(char *argv[], int sg_level, int *status);

int discrepancy_filename(char **filename);
int cpp_maybe(char **argv, char *input_fname, char **cpp_fname, pid_t *cpp_pid);

#endif //_HEADER_COMPILE_H
