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
#include "files.h"
#include "stringutils.h"
#include "trace.h"

/*
 * get the basename withou ext
 * the caller is responsible to free the return string
 */
char* get_basename_no_ext(char* sfile)
{
    char* basename_no_ext = NULL;
    char* dot = NULL;

    const char* basename = find_basename(sfile);
    basename_no_ext = strdup(basename);
    dot = find_extension(basename_no_ext);
    *dot = '\0';
    
    return basename_no_ext;
}

const char * find_basename(const char *sfile)
{
    if (!sfile)
        return sfile;

    const char* slash = strrchr(sfile, '/');
    if (slash == NULL || slash[1] == '\0')
        return sfile;
    return slash+1;
}


/**
 * Return a pointer to the extension, including the dot, or NULL.
 **/
char * find_extension(char *sfile)
{
    char *dot;

    dot = strrchr(sfile, '.');
    if (dot == NULL || dot[1] == '\0') {
        /* make sure there's space for one more character after the
         * dot */
        return NULL;
    }
    return dot;
}


const char * find_extension_const(const char *sfile) 
{
    /* The following intermediate variable works around a bug in gcc 4.2.3 where
     * for the code above gcc spuriously reports "warning: passing argument 1
     * of 'find_extension' discards qualifiers from pointer target type",
     * despite the explicit cast. */
    char *sfile_nonconst = (char *)sfile;
    return find_extension(sfile_nonconst);
}

/**
 * Work out whether @p sfile is source based on extension
 **/
int is_source(const char *sfile)
{
    const char *dot, *ext;
    dot = find_extension_const(sfile);
    if (!dot)
        return 0;
    ext = dot+1;

    /* you could expand this out further into a RE-like set of case
     * statements, but i'm not sure it's that important. */

    switch (ext[0]) {
    case 'i':
        return !strcmp(ext, "i")
            || !strcmp(ext, "ii");
    case 'c':
        return !strcmp(ext, "c")
            || !strcmp(ext, "cc")
            || !strcmp(ext, "cpp")
            || !strcmp(ext, "cxx")
            || !strcmp(ext, "cp")
            || !strcmp(ext, "c++");
    case 'C':
        return !strcmp(ext, "C");
    case 'm':
        return !strcmp(ext,"m")
            || !strcmp(ext,"mm")
            || !strcmp(ext,"mi")
            || !strcmp(ext,"mii");
    case 'M':
        return !strcmp(ext, "M");
#ifdef ENABLE_REMOTE_ASSEMBLE
    case 's':
        return !strcmp(ext, "s");
    case 'S':
        return !strcmp(ext, "S");
#endif
    default:
        return 0;
    }
}


/**
 * Decide whether @p filename is an object file, based on its
 * extension.
 **/
int is_object(const char *filename)
{
    const char *dot;
    dot = find_extension_const(filename);
    if (!dot)
        return 0;

    return !strcmp(dot, ".o");
}

/**
 * Does the extension of this file indicate that it is already
 * preprocessed?
 **/
int is_preprocessed(const char *sfile)
{
    const char *dot, *ext;
    dot = find_extension_const(sfile);
    if (!dot)
        return 0;
    ext = dot+1;

    switch (ext[0]) {
#ifdef ENABLE_REMOTE_ASSEMBLE
    case 's':
        /* .S needs to be run through cpp; .s does not */
        return !strcmp(ext, "s");
#endif
    case 'i':
        return !strcmp(ext, "i")
            || !strcmp(ext, "ii");
    case 'm':
        return !strcmp(ext, "mi")
            || !strcmp(ext, "mii");
    default:
        return 0;
    }
}

/* Some files should always be built locally... */
int source_needs_local(const char *filename)
{
    const char *p;

    p = find_basename(filename);

    if (str_startswith("conftest.", p) || str_startswith("tmp.conftest.", p)) {
        rs_trace("autoconf tests are run locally: %s", filename);
        return EXIT_MRCC_FAILED;
    }

    return 0;
}


static int set_file_extension(const char *sfile, const char *new_ext, char **ofile)
{
    char *dot, *o;

    o = strdup(sfile);
    if (!o) {
        rs_log_error("strdup failed (out of memory?)");
        return EXIT_MRCC_FAILED;
    }
    dot = find_extension(o);
    if (!dot) {
        rs_log_error("couldn't find extension in \"%s\"", o);
        return EXIT_MRCC_FAILED;
    }
    if (strlen(dot) < strlen(new_ext)) {
        rs_log_error("not enough space for new extension");
        return EXIT_MRCC_FAILED;
    }
    strcpy(dot, new_ext);
    *ofile = o;

    return 0;
}


/**
 * Work out the default object file name the compiler would use if -o
 * was not specified.  We don't need to worry about "a.out" because
 * we've already determined that -c or -S was specified.
 *
 * However, the compiler does put the output file in the current
 * directory even if the source file is elsewhere, so we need to strip
 * off all leading directories.
 *
 * @param sfile Source filename.  Assumed to match one of the
 * recognized patterns, otherwise bad things might happen.
 **/
int output_from_source(const char *sfile, const char *out_extn, char **ofile)
{
    char *slash;

    if ((slash = strrchr(sfile, '/')))
        sfile = slash+1;
    if (strlen(sfile) < 3) {
        rs_log_error("source file %s is bogus", sfile);
        return EXIT_MRCC_FAILED;
    }

    return set_file_extension(sfile, out_extn, ofile);
}


/**
 * If you preprocessed a file with extension @p e, what would you get?
 *
 * @param e original extension (e.g. ".c")
 *
 * @returns preprocessed extension, (e.g. ".i"), or NULL if
 * unrecognized.
 **/
const char * preproc_exten(const char *e)
{
    if (e[0] != '.')
        return NULL;
    e++;
    if (!strcmp(e, "i") || !strcmp(e, "c")) {
        return ".i";
    } else if (!strcmp(e, "c") || !strcmp(e, "cc")
               || !strcmp(e, "cpp") || !strcmp(e, "cxx")
               || !strcmp(e, "cp") || !strcmp(e, "c++")
               || !strcmp(e, "C") || !strcmp(e, "ii")) {
        return ".ii";
    } else if(!strcmp(e,"mi") || !strcmp(e, "m")) {
        return ".mi";
    } else if(!strcmp(e,"mii") || !strcmp(e,"mm")
                || !strcmp(e,"M")) {
        return ".mii";
    } else if (!strcasecmp(e, "s")) {
        return ".s";
    } else {
        return NULL;
    }
}


