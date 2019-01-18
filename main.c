// Copyright 2012 Rui Ueyama. Released under the MIT license.

// libgen.h is a header file that, uh, 'for historical reasons', provides 
// definitions for pattern matching functions. It's part of POSIX.
#include <libgen.h>

// General purpose standard library functions for the C programming language.
// Includes a random grab-bag of stuff.
#include <stdlib.h>

// String functions. Also memory management functions like memcpy() and 
// memset(), for some reason. 
#include <string.h>

// The 'sys' directory (usually, as a matter of convention) contains 
// system-specific header files. This one defines data types on UNIX systems.
#include <sys/types.h>

// Defines constants for use with waitpid(), which is a function that waits for
// a child process to stop or terminate.
#include <sys/wait.h>

// Defines a bunch of UNIX-related constants, including NULL, F_OK (for files),
// and access to the POSIX API. Usually this just means wrappers over some
// system call interfaces. Pretty important. 
#include <unistd.h>

// Include the main 8cc header file.
#include "8cc.h"

static char *infile;
static char *outfile;
static char *asmfile;
static bool dumpast;
static bool cpponly;
static bool dumpasm;
static bool dontlink;
static Buffer *cppdefs;
static Vector *tmpfiles = &EMPTY_VECTOR;

static void usage(int exitcode) {
    fprintf(exitcode ? stderr : stdout,
            "Usage: 8cc [ -E ][ -a ] [ -h ] <file>\n\n"
            "\n"
            "  -I<path>          add to include path\n"
            "  -E                print preprocessed source code\n"
            "  -D name           Predefine name as a macro\n"
            "  -D name=def\n"
            "  -S                Stop before assembly (default)\n"
            "  -c                Do not run linker (default)\n"
            "  -U name           Undefine name\n"
            "  -fdump-ast        print AST\n"
            "  -fdump-stack      Print stacktrace\n"
            "  -fno-dump-source  Do not emit source code as assembly comment\n"
            "  -o filename       Output to the specified file\n"
            "  -g                Do nothing at this moment\n"
            "  -Wall             Enable all warnings\n"
            "  -Werror           Make all warnings into errors\n"
            "  -O<number>        Does nothing at this moment\n"
            "  -m64              Output 64-bit code (default)\n"
            "  -w                Disable all warnings\n"
            "  -h                print this help\n"
            "\n"
            "One of -a, -c, -E or -S must be specified.\n\n");
    exit(exitcode);
}

static void delete_temp_files() {
    for (int i = 0; i < vec_len(tmpfiles); i++)
        unlink(vec_get(tmpfiles, i));
}

static char *base(char *path) {
    return basename(strdup(path));
}

static char *replace_suffix(char *filename, char suffix) {
    char *r = format("%s", filename);
    char *p = r + strlen(r) - 1;
    if (*p != 'c')
        error("filename suffix is not .c");
    *p = suffix;
    return r;
}

static FILE *open_asmfile() {
    if (dumpasm) {
        asmfile = outfile ? outfile : replace_suffix(base(infile), 's');
    } else {
        asmfile = format("/tmp/8ccXXXXXX.s");
        if (!mkstemps(asmfile, 2))
            perror("mkstemps");
        vec_push(tmpfiles, asmfile);
    }
    if (!strcmp(asmfile, "-"))
        return stdout;
    FILE *fp = fopen(asmfile, "w");
    if (!fp)
        perror("fopen");
    return fp;
}

static void parse_warnings_arg(char *s) {
    if (!strcmp(s, "error"))
        warning_is_error = true;
    else if (strcmp(s, "all"))
        error("unknown -W option: %s", s);
}

static void parse_f_arg(char *s) {
    if (!strcmp(s, "dump-ast"))
        dumpast = true;
    else if (!strcmp(s, "dump-stack"))
        dumpstack = true;
    else if (!strcmp(s, "no-dump-source"))
        dumpsource = false;
    else
        usage(1);
}

static void parse_m_arg(char *s) {
    if (strcmp(s, "64"))
        error("Only 64 is allowed for -m, but got %s", s);
}

// Function to parse the provided command line options.
static void parseopt(int argc, char **argv) {

    // Create a buffer called 'cppdefs'.
    cppdefs = make_buffer();

    // 'Infinite' loop
    for (;;) {

        // getopt() is a C library function used to parse command-line options.
        // Essentially, it is invoked multiple times to get each argument that
        // is an argument. The first argument is the index of the next argument,
        // which gets decremented, the second argument is the arguments 
        // themselves, and the final argument is a string containing characters
        // that define which arguments are legit. If the character is followed
        // by a ':', then the option needs an argument. 
        int opt = getopt(argc, argv, "I:ED:O:SU:W:acd:f:gm:o:hw");
        
        // getopt() returns -1 when there are no more options.
        if (opt == -1)
            break;

        // Switch on the option that was just parsed.
        switch (opt) {

        // 'I' is an option to add a file to the include path.
        case 'I': add_include_path(optarg); break;

        // 'E' is an option to run *only* the C preprocessor. Presumably, this was
        // useful during development of the preprocessor.
        case 'E': cpponly = true; break;

        // 'D' is an option to define a macro
        case 'D': {
            char *p = strchr(optarg, '=');
            if (p)
                *p = ' ';
            buf_printf(cppdefs, "#define %s\n", optarg);
            break;
        }
        case 'O': break;
        case 'S': dumpasm = true; break;
        case 'U':
            buf_printf(cppdefs, "#undef %s\n", optarg);
            break;
        case 'W': parse_warnings_arg(optarg); break;
        case 'c': dontlink = true; break;
        case 'f': parse_f_arg(optarg); break;
        case 'm': parse_m_arg(optarg); break;
        case 'g': break;
        case 'o': outfile = optarg; break;
        case 'w': enable_warning = false; break;
        case 'h':
            usage(0);
        default:
            usage(1);
        }
    }
    if (optind != argc - 1)
        usage(1);

    if (!dumpast && !cpponly && !dumpasm && !dontlink)
        error("One of -a, -c, -E or -S must be specified");
    infile = argv[optind];
}

char *get_base_file() {
    return infile;
}

static void preprocess() {
    for (;;) {
        Token *tok = read_token();
        if (tok->kind == TEOF)
            break;
        if (tok->bol)
            printf("\n");
        if (tok->space)
            printf(" ");
        printf("%s", tok2s(tok));
    }
    printf("\n");
    exit(0);
}

// The entry point of the program!
int main(int argc, char **argv) {

    // Typically, I/O functionality is implemented with 'buffering', which means
    // that calls to (say) printf() are stored in a buffer until that buffer is
    // filled up, at which point the system call is made to actually print to
    // stdout. This is done because system calls are expensive (1000s of cycles)
    // compared to function calls.
    // This line code sets the buffer for stdout to be NULL, which means that
    // buffering shouldn't happen at all for this program.
    setbuf(stdout, NULL);
    
    // atexit() does what you would expect, if you were able to actually parse
    // the name. atexit() = at_exit(); the first line ensures that the temp 
    // files are deleted when the program exits. The second line is called if
    // atexit() returns a nonzero value (i.e. an error has occurred).
    // perror() takes the name of a function that was called, in this case
    // it was atexit(), and then prints the current value of 'errno' 
    // (a POSIX variable) converted into a human-readable error message for the
    // given function.
    if (atexit(delete_temp_files))
        perror("atexit");
    
    // Call to parse the provided command-line options.
    parseopt(argc, argv);


    lex_init(infile);
    cpp_init();
    parse_init();
    set_output_file(open_asmfile());
    if (buf_len(cppdefs) > 0)
        read_from_string(buf_body(cppdefs));

    if (cpponly)
        preprocess();

    Vector *toplevels = read_toplevels();
    for (int i = 0; i < vec_len(toplevels); i++) {
        Node *v = vec_get(toplevels, i);
        if (dumpast)
            printf("%s", node2s(v));
        else
            emit_toplevel(v);
    }

    close_output_file();

    if (!dumpast && !dumpasm) {
        if (!outfile)
            outfile = replace_suffix(base(infile), 'o');
        pid_t pid = fork();
        if (pid < 0) perror("fork");
        if (pid == 0) {
            execlp("as", "as", "-o", outfile, "-c", asmfile, (char *)NULL);
            perror("execl failed");
        }
        int status;
        waitpid(pid, &status, 0);
        if (status < 0)
            error("as failed");
    }
    return 0;
}
