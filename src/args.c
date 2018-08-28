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
#include "args.h"



unsigned int argv_len(char **a)
{
    unsigned int i;
    for (i = 0; a[i]; i++);                                                                                                        
    return i;
}


/* Copy an argv array, adding extra NULL elements to the end to allow for
 * adding more arguments later.
 */
int copy_argv(char **from, char ***out, int delta)
{
    char **b;
    int l, i;

    l = argv_len(from);
    b = (char**) malloc((l+1+delta) * (sizeof from[0]));
                                                                             
    if (b == NULL) {
        rs_log_error("failed to allocate copy of argv");
        return EXIT_OUT_OF_MEMORY;
    }
                                                                                                                       
    for (i = 0; i < l; i++) {
        if ((b[i] = strdup(from[i])) == NULL) {
            rs_log_error("failed to duplicate element %d", i);
            return EXIT_OUT_OF_MEMORY;
        }
    }
                                                                                                                       
    b[l] = NULL;

    *out = b;
    return 0;
}

int find_compiler(char **argv, char ***out_argv)
{
    int ret;
    if (argv[1][0] == '-'
        || is_source(argv[1])
        || is_object(argv[1])) {
        if ((ret = copy_argv(argv, out_argv, 0)) != 0) {
            return ret;
        }

        /* change "mrcc -c foo.c" -> "cc -c foo.c" */
        free((*out_argv)[0]);
        (*out_argv)[0] = strdup("cc");
        if ((*out_argv)[0] == NULL) {
            return EXIT_OUT_OF_MEMORY;
        }
        return 0;
    } else {
        /* skip "mrcc", point to "gcc -c foo.c"  */
        return copy_argv(argv + 1, out_argv, 0);
    }
}



/* Subroutine of expand_preprocessor_options().
 * Calculate how many extra arguments we'll need to convert
 * a "-Wp,..." option into regular gcc options.
 * Returns the number of extra arguments needed.
 */
static int count_extra_args(char *dash_Wp_option) {
    int extra_args = 0;
    char *comma = dash_Wp_option + strlen("-Wp");
    while (comma != NULL) {
        char *opt = comma + 1;
        comma = strchr(opt, ',');
        if (str_startswith("-MD,", opt) ||
            str_startswith("-MMD,", opt))
        {
            char *filename = comma + 1;
            comma = strchr(filename, ',');
            extra_args += 3;  /* "-MD", "-MF", filename. */
        } else {
            extra_args++;
        }
    }
    return extra_args;
}


/* Subroutine of expand_preprocessor_options().
 * Convert a "-Wp,..." option into one or more regular gcc options.
 * Copy the resulting gcc options to dest_argv, which should be
 * pre-allocated by the caller.
 * Destructively modifies dash_Wp_option as it goes.
 * Returns 0 on success, nonzero for error (out of memory).
 */
static int copy_extra_args(char **dest_argv, char *dash_Wp_option,
                           int extra_args) {
    int i = 0;
    char *comma = dash_Wp_option + strlen("-Wp");
    while (comma != NULL) {
        char *opt = comma + 1;
        comma = strchr(opt, ',');
        if (comma) *comma = '\0';
        dest_argv[i] = strdup(opt);
        if (!dest_argv[i]) return EXIT_OUT_OF_MEMORY;
        i++;
        if (strcmp(opt, "-MD") == 0 || strcmp(opt, "-MMD") == 0) {
            char *filename;
            if (!comma) {
                rs_log_warning("'-Wp,-MD' or '-Wp,-MMD' option is missing "
                               "filename argument");
                break;
            }
            filename = comma + 1;
            comma = strchr(filename, ',');
            if (comma) *comma = '\0';
            dest_argv[i] = strdup("-MF");
            if (!dest_argv[i]) return EXIT_OUT_OF_MEMORY;
            i++;
            dest_argv[i] = strdup(filename);
            if (!dest_argv[i]) return EXIT_OUT_OF_MEMORY;
            i++;
        }
    }
    assert(i == extra_args);
    return 0;
}


int argv_append(char **argv, char *toadd)
{
    int l = argv_len(argv);
    argv[l] = toadd;
    argv[l+1] = NULL;           /* just make sure */
    return 0;
}

static void note_compiled(const char *input_file, const char *output_file)
{
    const char *input_base, *output_base;

    input_base = find_basename(input_file);
    output_base = find_basename(output_file);

    rs_log(RS_LOG_INFO|RS_LOG_NONAME,     "compile from %s to %s", input_base, output_base);
}


/**
 * Parse arguments, extract ones we care about, and also work out
 * whether it will be possible to distribute this invocation remotely.
 *
 * This is a little hard because the cc argument rules are pretty complex, but
 * the function still ought to be simpler than it already is.
 *
 * This function makes a copy of the arguments, modified to ensure that
 * the arguments include '-o <filename>'.  This is returned in *ret_newargv.
 * The copy is dynamically allocated and the caller is responsible for
 * deallocating it.
 *
 * @returns 0 if it's ok to distribute this compilation, or an error code.
 **/
int scan_args(char *argv[], char **input_file, char **output_file, char ***ret_newargv)
{
    int seen_opt_c = 0, seen_opt_s = 0;
    int i;
    char *a;
    int ret;

     /* allow for -o foo.o */
    if ((ret = copy_argv(argv, ret_newargv, 2)) != 0)
        return ret;
    argv = *ret_newargv;

    /* FIXME: new copy of argv is leaked */

    // trace_argv("scanning arguments", argv);

    /* Things like "mrcc -c hello.c" with an implied compiler are
     * handled earlier on by inserting a compiler name.  At this
     * point, argv[0] should always be a compiler name. */
    if (argv[0][0] == '-') {
        rs_log_error("unrecognized mrcc option: %s", argv[0]);
        exit(EXIT_BAD_ARGUMENTS);
    }

    *input_file = *output_file = NULL;

    for (i = 0; (a = argv[i]); i++) {
        if (a[0] == '-') {
            if (!strcmp(a, "-E")) {
                rs_trace("-E call for cpp must be local");
                return EXIT_MRCC_FAILED;
            } else if (!strcmp(a, "-MD") || !strcmp(a, "-MMD")) {
                /* These two generate dependencies as a side effect.  They
                 * should work with the way we call cpp. */
            } else if (!strcmp(a, "-MG") || !strcmp(a, "-MP")) {
                /* These just modify the behaviour of other -M* options and do
                 * nothing by themselves. */
            } else if (!strcmp(a, "-MF") || !strcmp(a, "-MT") ||
                       !strcmp(a, "-MQ")) {
                /* As above but with extra argument. */
                i++;
            } else if (!strncmp(a, "-MF", 3) || !strncmp(a, "-MT", 3) ||
                       !strncmp(a, "-MQ", 3)) {
                /* As above, without extra argument. */
            } else if (a[1] == 'M') {
                /* -M(anything else) causes the preprocessor to
                    produce a list of make-style dependencies on
                    header files, either to stdout or to a local file.
                    It implies -E, so only the preprocessor is run,
                    not the compiler.  There would be no point trying
                    to distribute it even if we could. */
                rs_trace("%s implies -E (maybe) and must be local", a);
                return EXIT_MRCC_FAILED;
            } else if (!strcmp(a, "-march=native")) {
                rs_trace("-march=native generates code for local machine; ""must be local");
                return EXIT_MRCC_FAILED;
            } else if (!strcmp(a, "-mtune=native")) {
                rs_trace("-mtune=native optimizes for local machine; ""must be local");
                return EXIT_MRCC_FAILED;
            } else if (str_startswith("-Wa,", a)) {
                /* Look for assembler options that would produce output
                 * files and must be local.
                 *
                 * Writing listings to stdout could be supported but it might
                 * be hard to parse reliably. */
                if (strstr(a, ",-a") || strstr(a, "--MD")) {
                    rs_trace("%s must be local", a);
                    return EXIT_MRCC_FAILED;
                }
            } else if (str_startswith("-specs=", a)) {
                rs_trace("%s must be local", a);
                return EXIT_MRCC_FAILED;
            } else if (!strcmp(a, "-S")) {
                seen_opt_s = 1;
                // rs_trace("-S is processed by local");
                // return EXIT_MRCC_FAILED;
            } else if (!strcmp(a, "-fprofile-arcs")
                       || !strcmp(a, "-ftest-coverage")) {
                rs_log_info("compiler will emit profile info; must be local");
                return EXIT_MRCC_FAILED;
            } else if (!strcmp(a, "-frepo")) {
                rs_log_info("compiler will emit .rpo files; must be local");
                return EXIT_MRCC_FAILED;
            } else if (str_startswith("-x", a)) {
                rs_log_info("gcc's -x handling is complex; running locally");
                return EXIT_MRCC_FAILED;
            } else if (str_startswith("-dr", a)) {
                rs_log_info("gcc's debug option %s may write extra files; ""running locally", a);
                return EXIT_MRCC_FAILED;
            } else if (!strcmp(a, "-c")) {
                seen_opt_c = 1;
            } else if (!strcmp(a, "-o")) {
                /* Whatever follows must be the output */
                a = argv[++i];
                goto GOT_OUTPUT;
            } else if (str_startswith("-o", a)) {
                a += 2;         /* skip "-o" */
                goto GOT_OUTPUT;
            }
        } else {
            if (is_source(a)) {
                rs_trace("found input file \"%s\"", a);
                if (*input_file) {
                    rs_log_info("do we have two inputs?  i give up");
                    return EXIT_MRCC_FAILED;
                }
                *input_file = a;
            } else if (str_endswith(".o", a)) {
              GOT_OUTPUT:
                rs_trace("found object/output file \"%s\"", a);
                if (*output_file) {
                    rs_log_info("called for link?  i give up");
                    return EXIT_MRCC_FAILED;
                }
                *output_file = a;
            }
        }
    }

    /* TODO: ccache has the heuristic of ignoring arguments that are not
     * extant files when looking for the input file; that's possibly
     * worthwile.  Of course we can't do that on the server. */

    if (!seen_opt_c && !seen_opt_s) {
        rs_log_info("compiler apparently called not for compile");
        return EXIT_MRCC_FAILED;
    }

    if (!*input_file) {
        rs_log_info("no visible input file");
        return EXIT_MRCC_FAILED;
    }

    if (source_needs_local(*input_file))
        return EXIT_MRCC_FAILED;

    if (!*output_file) {
        /* This is a commandline like "gcc -c hello.c".  They want
         * hello.o, but they don't say so.  For example, the Ethereal
         * makefile does this.
         *
         * Note: this doesn't handle a.out, the other implied
         * filename, but that doesn't matter because it would already
         * be excluded by not having -c or -S.
         */
        char *ofile;

        /* -S takes precedence over -c, because it means "stop after
         * preprocessing" rather than "stop after compilation." */
        if (seen_opt_s) {
            if (output_from_source(*input_file, ".s", &ofile))
                return EXIT_MRCC_FAILED;
        } else if (seen_opt_c) {
            if (output_from_source(*input_file, ".o", &ofile))
                return EXIT_MRCC_FAILED;
        } else {
            rs_log_crit("this can't be happening(%d)!", __LINE__);
            return EXIT_MRCC_FAILED;
        }
        rs_log_info("no visible output file, going to add \"-o %s\" at end",ofile);
        argv_append(argv, strdup("-o"));
        argv_append(argv, ofile);
        *output_file = ofile;
    }

    note_compiled(*input_file, *output_file);

    if (strcmp(*output_file, "-") == 0) {
        /* Different compilers may treat "-o -" as either "write to
         * stdout", or "write to a file called '-'".  We can't know,
         * so we just always run it locally.  Hopefully this is a
         * pretty rare case. */
        rs_log_info("output to stdout?  running locally");
        return EXIT_MRCC_FAILED;
    }

    return 0;
}

/*
 * Convert any "-Wp," options into regular gcc options.
 * We do this because it simplifies the command-line
 * option handling elsewhere; this is the only place
 * that needs to parse "-Wp," options.
 * Returns 0 on success, nonzero for error (out of memory).
 *
 * The argv array pointed to by argv_ptr when this function
 * is called must have been dynamically allocated.  It remains
 * the caller's responsibility to deallocate it.
 */
int expand_preprocessor_options(char ***argv_ptr)
{
    int i, j, ret;
    char **argv = *argv_ptr;
    char **new_argv;
    int argc = argv_len(argv);
    for (i = 0; argv[i]; i++) {
        if (str_startswith("-Wp,", argv[i])) {
            /* First, calculate how many extra arguments we'll need. */
            int extra_args = count_extra_args(argv[i]);
            assert(extra_args >= 1);

            new_argv = calloc(argc + extra_args, sizeof(char *));
            if (!new_argv) {
                return EXIT_OUT_OF_MEMORY;
            }
            for (j = 0; j < i; j++) {
                new_argv[j] = argv[j];
            }
            if ((ret = copy_extra_args(new_argv + i, argv[i],
                                       extra_args)) != 0) {
                free(new_argv);
                return ret;
            }
            for (j = i + 1; j <= argc; j++) {
                new_argv[j + extra_args - 1] = argv[j];
            }
            free(argv);
            *argv_ptr = argv = new_argv;
        }
    }
    return 0;
}

/* Free a malloc'd argv structure.  Only safe when the array and all its
 * components were malloc'd. */
void free_argv(char **argv)
{
    char **a;

    for (a = argv; *a != NULL; a++)
        free(*a);
    free(argv);
}

/**
 * Remove "-o" options from argument list.
 *
 * This is used when running the preprocessor, when we just want it to write
 * to stdout, which is the default when no -o option is specified.
 *
 * Structurally similar to strip_local_args()
 **/
int strip_dasho(char **from, char ***out_argv)
{
    char **to;
    int from_i, to_i;
    int from_len;

    from_len = argv_len(from);
    *out_argv = to = malloc((from_len + 1) * sizeof (char *));

    if (!to) {
        rs_log_error("failed to allocate space for arguments");
        return EXIT_OUT_OF_MEMORY;
    }

    /* skip through argv, copying all arguments but skipping ones that
     * ought to be omitted */
    for (from_i = to_i = 0; from[from_i]; ) {
        if (!strcmp(from[from_i], "-o")) {
            /* skip "-o  FILE" */
            from_i += 2;
        }
        else if (str_startswith("-o", from[from_i])) {
            /* skip "-oFILE" */
            from_i++;
        }
        else {
            to[to_i++] = from[from_i++];
        }
    }

    /* NULL-terminate */
    to[to_i] = NULL;

    trace_argv("result", to);

    return 0;
}


/**
 * Strip arguments like -D and -I from a command line, because they do
 * not need to be passed across the wire.  This covers options for
 * both the preprocess and link phases, since they should never happen
 * remotely.
 *
 * In the case where we inadvertently do cause preprocessing to happen
 * remotely, it is possible that omitting these options will make
 * failure more obvious and avoid false success.
 *
 * Giving -L on a compile-only command line is a bit wierd, but it is
 * observed to happen in Makefiles that are not strict about CFLAGS vs
 * LDFLAGS, etc.
 *
 * NOTE: gcc-3.2's manual in the "preprocessor options" section
 * describes some options, such as -d, that only take effect when
 * passed directly to cpp.  When given to gcc they have different
 * meanings.
 *
 * The value stored in '*out_argv' is malloc'd, but the arguments that
 * are pointed to by that array are aliased with the values pointed
 * to by 'from'.  The caller is responsible for calling free() on
 * '*out_argv'.
 **/
int strip_local_args(char **from, char ***out_argv)
{
    char **to;
    int from_i, to_i;
    int from_len;

    from_len = argv_len(from);
    *out_argv = to = malloc((from_len + 1) * sizeof (char *));

    if (!to) {
        rs_log_error("failed to allocate space for arguments");
        return EXIT_OUT_OF_MEMORY;
    }

    /* skip through argv, copying all arguments but skipping ones that
     * ought to be omitted */
    for (from_i = to_i = 0; from[from_i]; from_i++) {
        if (str_equal("-D", from[from_i])
            || str_equal("-I", from[from_i])
            || str_equal("-U", from[from_i])
            || str_equal("-L", from[from_i])
            || str_equal("-l", from[from_i])
            || str_equal("-MF", from[from_i])
            || str_equal("-MT", from[from_i])
            || str_equal("-MQ", from[from_i])
            || str_equal("-include", from[from_i])
            || str_equal("-imacros", from[from_i])
            || str_equal("-iprefix", from[from_i])
            || str_equal("-iwithprefix", from[from_i])
            || str_equal("-isystem", from[from_i])
            || str_equal("-iwithprefixbefore", from[from_i])
            || str_equal("-idirafter", from[from_i])) {
            /* skip next word, being option argument */
            if (from[from_i+1])
                from_i++;
        }
        else if (str_startswith("-Wp,", from[from_i])
                 || str_startswith("-Wl,", from[from_i])
                 || str_startswith("-D", from[from_i])
                 || str_startswith("-U", from[from_i])
                 || str_startswith("-I", from[from_i])
                 || str_startswith("-l", from[from_i])
                 || str_startswith("-L", from[from_i])
                 || str_startswith("-MF", from[from_i])
                 || str_startswith("-MT", from[from_i])
                 || str_startswith("-MQ", from[from_i])) {
            /* Something like "-DNDEBUG" or
             * "-Wp,-MD,.deps/nsinstall.pp".  Just skip this word */
            ;
        }
        else if (str_equal("-undef", from[from_i])
                 || str_equal("-nostdinc", from[from_i])
                 || str_equal("-nostdinc++", from[from_i])
                 || str_equal("-MD", from[from_i])
                 || str_equal("-MMD", from[from_i])
                 || str_equal("-MG", from[from_i])
                 || str_equal("-MP", from[from_i])) {
            /* Options that only affect cpp; skip */
            ;
        }
        else {
            to[to_i++] = from[from_i];
        }
    }

    /* NULL-terminate */
    to[to_i] = NULL;

    trace_argv("result", to);

    return 0;
}


/**
 * Convert an argv array to printable form for debugging output.
 *
 * @note The result is not necessarily properly quoted for passing to
 * shells.
 *
 * @return newly-allocated string containing representation of
 * arguments.
 **/
char *argv_tostr(char **a)
{
    int l, i;
    char *s, *ss;

    /* calculate total length */
    for (l = 0, i = 0; a[i]; i++) {
        l += strlen(a[i]) + 3;  /* two quotes and space */
    }

    ss = s = malloc((size_t) l + 1);
    if (!s) {
        rs_log_crit("failed to allocate %d bytes", l+1);
        exit(EXIT_OUT_OF_MEMORY);
    }

    for (i = 0; a[i]; i++) {
        /* kind of half-assed quoting; won't handle strings containing
         * quotes properly, but good enough for debug messages for the
         * moment. */
        int needs_quotes = (strpbrk(a[i], " \t\n\"\';") != NULL);
        if (i)
            *ss++ = ' ';
        if (needs_quotes)
            *ss++ = '"';
        strcpy(ss, a[i]);
        ss += strlen(a[i]);
        if (needs_quotes)
            *ss++ = '"';
    }
    *ss = '\0';

    return s;
}



/**
 * Used to change "-c" or "-S" to "-E", so that we get preprocessed
 * source.
 **/
int set_action_opt(char **a, const char *new_c)
{
    int gotone = 0;

    for (; *a; a++)
        if (!strcmp(*a, "-c") || !strcmp(*a, "-S")) {
            *a = strdup(new_c);
            if (*a == NULL) {
                rs_log_error("strdup failed");
                exit(EXIT_OUT_OF_MEMORY);
            }
            gotone = 1;
            /* keep going; it's not impossible they wrote "gcc -c -c
             * -c hello.c" */
        }

    if (!gotone) {
        rs_log_error("failed to find -c or -S");
        return EXIT_MRCC_FAILED;
    } else {
        return 0;
    }
}


