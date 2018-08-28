// mrcc - A C Compiler system on MapReduce
// Zhiqiang Ma, https://www.ericzma.com

#ifndef _HEADER_UTILS_H
# define _HEADER_UTILS_H

/**
 * Common exit codes.
 *
 * These need to be in [1,255] so that they can be used as exit() codes.  They
 * are fairly high so that they're not confused with the real error code from
 * gcc.
 *
 **/
enum mrcc_exitcode {
    EXIT_MRCC_FAILED              = 100, /**< General failure */
    EXIT_BAD_ARGUMENTS            = 101,
    EXIT_BIND_FAILED              = 102,
    EXIT_CONNECT_FAILED           = 103,
    EXIT_COMPILER_CRASHED         = 104,
    EXIT_OUT_OF_MEMORY            = 105,
    EXIT_BAD_HOSTSPEC             = 106,
    EXIT_IO_ERROR                 = 107,
    EXIT_TRUNCATED                = 108,
    EXIT_PROTOCOL_ERROR           = 109,
    EXIT_COMPILER_MISSING         = 110, /**< Compiler executable not found */
    EXIT_RECURSION                = 111, /**< mrcc called itself */
    EXIT_SETUID_FAILED            = 112, /**< Failed to discard privileges */
    EXIT_ACCESS_DENIED            = 113, /**< Network access denied */
    EXIT_BUSY                     = 114, /**< In use by another process. */
    EXIT_NO_SUCH_FILE             = 115,
    EXIT_NO_HOSTS                 = 116,
    EXIT_GONE                     = 117, /**< No longer relevant */
    EXIT_TIMEOUT                  = 118,
    EXIT_PUT_CPP_FS_FAILED        = 119, /* put cpp file to net fs failed */
    EXIT_PUT_CONFIG_FS_FAILED     = 120, /* put config file to net fs failed */
    EXIT_GET_CPP_FS_FAILED        = 121,
    EXIT_GET_CONFIG_FS_FAILED     = 122,
    EXIT_CALL_MAPPER_FAILED       = 123,
    EXIT_MAPPER_FAILED            = 124
};

enum cpp_where {
    /* wierd values to catch errors */
    MRCC_CPP_ON_CLIENT     = 42,
    MRCC_CPP_ON_SERVER
};

/**
 * A simple linked list of host definitions.  All strings are mallocd.
 **/
struct hostdef {
    enum {
        MRCC_MODE_TCP = 1,
        MRCC_MODE_SSH,
        MRCC_MODE_LOCAL
    } mode;
    char * user;
    char * hostname;
    int port;
    char * ssh_command;

    /** Mark the host as up == 1, by default, or down == 0, if !hostname */
    int is_up;

    /** Number of tasks that can be dispatched concurrently to this machine. */
    int n_slots;

    /** The full name of this host, taken verbatim from the host
     * definition. **/
    char * hostdef_string;

    /** Where are we doing preprocessing? */
    enum cpp_where cpp_where;

    struct hostdef *next;
};


enum protover {
    MRCC_VER_1   = 1,            /**< vanilla */
    MRCC_VER_2   = 2,            /**< LZO sprinkles */
    MRCC_VER_3   = 3             /**< server-side cpp */
};


enum compress {
    /* wierd values to catch errors */
    MRCC_COMPRESS_NONE     = 69,
    MRCC_COMPRESS_LZO1X
};



int set_path(const char *newpath);

int getenv_bool(const char *name, int default_value);

char *abspath(const char *path, int path_len);

void mrcc_exit(int exitcode);

int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y);

int get_protover_from_features(enum compress compr,
                                   enum cpp_where cpp_where,
                                   enum protover *protover);
int support_masquerade(char *argv[], const char *progname, int *did_masquerade);
int new_pgrp(void);
void ignore_sighup(void);
int ignore_sigpipe(int val);

void client_catch_signals(void);

#endif //_HEADER_UTILS_H
