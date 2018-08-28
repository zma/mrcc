// mrcc - A C Compiler system on MapReduce
// Zhiqiang Ma, https://www.ericzma.com

#ifndef _HEADER_EXEC_H
# define _HEADER_EXEC_H

// include for struct host_def
#include "utils.h"

extern const int timeout_null_fd;

int redirect_fd(int fd, const char *fname, int mode);
int redirect_fds(const char *stdin_file, const char *stdout_file, const char *stderr_file);
int spawn_child(char **argv, pid_t *pidptr, const char *stdin_file, const char *stdout_file, const char *stderr_file);

void note_execution(struct hostdef *host, char **argv);

int collect_child(const char *what, pid_t pid, int *wait_status, int in_fd);
int critique_status(int status,
                        const char *command,
                        const char *input_fname,
                        struct hostdef *host,
                        int verbose);


#endif //_HEADER_EXEC_H
