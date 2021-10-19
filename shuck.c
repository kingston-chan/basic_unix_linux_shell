////////////////////////////////////////////////////////////////////////
// COMP1521 21t2 -- Assignment 2 -- shuck, A Simple Shell
// <https://www.cse.unsw.edu.au/~cs1521/21T2/assignments/ass2/index.html>
//
// Written by Kingston Chan (z5362176) on 25/07/2021.
//
// 2021-07-12    v1.0    Team COMP1521 <cs1521@cse.unsw.edu.au>
// 2021-07-21    v1.1    Team COMP1521 <cs1521@cse.unsw.edu.au>
//     * Adjust qualifiers and attributes in provided code,
//       to make `dcc -Werror' happy.
//

#include <sys/types.h>

#include <sys/stat.h>
#include <sys/wait.h>

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shuck_builtins.h"
#include "shuck_io.h"
#include "shuck_helper.h"

#define LAST_COMMAND -1

//
// Interactive prompt:
//     The default prompt displayed in `interactive' mode --- when both
//     standard input and standard output are connected to a TTY device.
//
static const char *const INTERACTIVE_PROMPT = "shuck& ";

//
// Default path:
//     If no `$PATH' variable is set in Shuck's environment, we fall
//     back to these directories as the `$PATH'.
//
static const char *const DEFAULT_PATH = "/bin:/usr/bin";

//
// Default history shown:
//     The number of history items shown by default; overridden by the
//     first argument to the `history' builtin command.
//     Remove the `unused' marker once you have implemented history.
//
static const int DEFAULT_HISTORY_SHOWN __attribute__((unused)) = 10;

//
// Input line length:
//     The length of the longest line of input we can read.
//
static const size_t MAX_LINE_CHARS = 1024;

//
// Special characters:
//     Characters that `tokenize' will return as words by themselves.
//
static const char *const SPECIAL_CHARS = "!><|";

//
// Word separators:
//     Characters that `tokenize' will use to delimit words.
//
static const char *const WORD_SEPARATORS = " \t\r\n";


static void execute_command(char **words, char **path, char **environment);
static void do_exit(char **words, char **path);
static char **tokenize(char *s, char *separators, char *special_chars);
static void free_tokens(char **tokens);

static FILE *read_shuck_hist();
static void execute_nth_command(FILE *f, int n, char **path, char **env);
static int is_integer(char *word);
static int validate(char **glob_words);
static int check_program(char **glob_words, char **path, char **env, 
                         char *program, char *input_file);

int main (void)
{
    // Ensure `stdout' is line-buffered for autotesting.
    setlinebuf(stdout);

    // Environment variables are pointed to by `environ', an array of
    // strings terminated by a NULL value -- something like:
    //     { "VAR1=value", "VAR2=value", NULL }
    extern char **environ;

    // Grab the `PATH' environment variable for our path.
    // If it isn't set, use the default path defined above.
    char *pathp;
    if ((pathp = getenv("PATH")) == NULL) {
        pathp = (char *) DEFAULT_PATH;
    }
    char **path = tokenize(pathp, ":", "");

    // Should this shell be interactive?
    bool interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);

    // Main loop: print prompt, read line, execute command
    while (1) {
        // If `stdout' is a terminal (i.e., we're an interactive shell),
        // print a prompt before reading a line of input.
        if (interactive) {
            fputs(INTERACTIVE_PROMPT, stdout);
            fflush(stdout);
        }

        char line[MAX_LINE_CHARS];
        if (fgets(line, MAX_LINE_CHARS, stdin) == NULL)
            break;

        // Tokenise and execute the input line.
        char **command_words =
            tokenize(line, (char *) WORD_SEPARATORS, (char *) SPECIAL_CHARS);
        execute_command(command_words, path, environ);
        free_tokens(command_words);
    }

    free_tokens(path);
    return 0;
}


//
// Execute a command, and wait until it finishes.
//
//  * `words': a NULL-terminated array of words from the input command line
//  * `path': a NULL-terminated array of directories to search in;
//  * `environment': a NULL-terminated array of environment variables.
//
static void execute_command(char **words, char **path, char **environment)
{
    assert(words != NULL);
    assert(path != NULL);
    assert(environment != NULL);
    
    char *program = words[0];

    if (program == NULL) {
        // nothing to do
        return;
    }
    
    if (strcmp(program, "exit") == 0) {
        add_to_history(words);
        do_exit(words, path);
        // `do_exit' will only return if there was an error.
        return;
    }

    // Expand pattern words
    char **glob_words = init_glob_words(words);
    if (glob_words == NULL) {
        return;
    }

    // First word could have been pattern
    // so need to update new program
    program = glob_words[0];

    // Ensures that command line has valid I/O redirections
    // pipes and if there are builtin commands in command line, that they
    // do not have I/O redirections and pipes aswell
    if (validate(glob_words)) {
        free_array(glob_words);
        return;
    }
    

    // Change directory
    if (!strcmp(program, "cd")) {
        if (change_directory(glob_words)) {
            add_to_history(words);
        }
        free_array(glob_words);
        return;
    }

    // Show current directory
    if (!strcmp(program, "pwd")) {
        if (current_directory(glob_words)) {
            add_to_history(words);
        }
        free_array(glob_words);
        return;
    }

    // Check if progam is executable
    int prog = check_program(glob_words, path, environment, program, NULL);
    if (prog) { // Program is executable
        add_to_history(words);
        free_array(glob_words);
        return;
    }

    // Print nth last history commands
    if (!strcmp(program, "history")) {
        // Check if valid argument size
        if (array_size(glob_words) > 2) {
            fprintf(stderr, "history: too many arguments\n");
            free_array(glob_words);
            add_to_history(words);
            return;
        }

        FILE *f = read_shuck_hist();

        if (words[1] == NULL) {
            // Get path to shuck_history file
            // Print last 10 commands in 
            // history file
            if (f != NULL) {
                print_nth_history(f, DEFAULT_HISTORY_SHOWN);
                fclose(f);
            }
        }
        else {
            int n;
            if ((n = is_integer(words[1])) > 0 && f != NULL) {
                print_nth_history(f, n);
            }
        }
        free_array(glob_words);
        add_to_history(words);
        return;
    }

    // Execute last nth program from shuck_history
    if (!strcmp(program, "!")) {
        // Check if valid arguments size
        if (array_size(glob_words) > 2) {
            fprintf(stderr, "!: too many arguments\n");
            free_array(glob_words);
            return;
        }

        FILE *f = read_shuck_hist();
        // Execute last command in history
        if (f == NULL) {
            fprintf(stderr, "!: invalid history reference\n");
        } 
        // Execute last command
        else if (words[1] == NULL) {
            execute_nth_command(f, LAST_COMMAND, path, environment);
            fclose(f);
        }
        // Execute nth command in history if valid
        else {
            int n;
            if ((n = is_integer(words[1])) >= 0) {
                execute_nth_command(f, n, path, environment);
                fclose(f);
            }
        }
        free_array(glob_words);
        return;
    }

    // Input redirection
    if (!strcmp(program, "<")) {
        char *filename = glob_words[1];
        struct stat s;

        // Check filename is valid
        if (stat(filename, &s) != 0) {
            perror(filename);
            free_array(glob_words);
            return;
        }
        // Progam is at glob_words[2] 
        program = glob_words[2];
        int p = check_program(glob_words, path, environment, program, 
                              filename);
        if (p) { // Program is executable
            add_to_history(words);
            free_array(glob_words);
            return;
        }
    }

    // Command could not be executed
    fprintf(stderr, "%s: command not found\n", program);
    free_array(glob_words);
    add_to_history(words);
    return;
}


//
// Implement the `exit' shell built-in, which exits the shell.
//
// Synopsis: exit [exit-status]
// Examples:
//     % exit
//     % exit 1
//
static void do_exit(char **words, char **path)
{
    assert(words != NULL);
    assert(strcmp(words[0], "exit") == 0);

    int exit_status = 0;

    if (words[1] != NULL && words[2] != NULL) {
        // { "exit", "word", "word", ... }
        fprintf(stderr, "exit: too many arguments\n");
        return;
    } 
    else if (words[1] != NULL) {
        // { "exit", something, NULL }
        char *endptr;
        exit_status = (int) strtol(words[1], &endptr, 10);
        if (*endptr != '\0') {
            fprintf(stderr, "exit: %s: numeric argument required\n", words[1]);
        }
        return;
    }

    free_tokens(words);
    free_tokens(path);
    
    exit(exit_status);
}

//
// Split a string 's' into pieces by any one of a set of separators.
//
// Returns an array of strings, with the last element being `NULL'.
// The array itself, and the strings, are allocated with `malloc(3)';
// the provided `free_token' function can deallocate this.
//
static char **tokenize(char *s, char *separators, char *special_chars)
{
    size_t n_tokens = 0;

    // Allocate space for tokens.  We don't know how many tokens there
    // are yet --- pessimistically assume that every single character
    // will turn into a token.  (We fix this later.)
    char **tokens = calloc((strlen(s) + 1), sizeof *tokens);
    assert(tokens != NULL);

    while (*s != '\0') {
        // We are pointing at zero or more of any of the separators.
        // Skip all leading instances of the separators.
        s += strspn(s, separators);

        // Trailing separators after the last token mean that, at this
        // point, we are looking at the end of the string, so:
        if (*s == '\0') {
            break;
        }

        // Now, `s' points at one or more characters we want to keep.
        // The number of non-separator characters is the token length.
        size_t length = strcspn(s, separators);
        size_t length_without_specials = strcspn(s, special_chars);
        if (length_without_specials == 0) {
            length_without_specials = 1;
        }
        if (length_without_specials < length) {
            length = length_without_specials;
        }

        // Allocate a copy of the token.
        char *token = strndup(s, length);
        assert(token != NULL);
        s += length;

        // Add this token.
        tokens[n_tokens] = token;
        n_tokens++;
    }

    // Add the final `NULL'.
    tokens[n_tokens] = NULL;

    // Finally, shrink our array back down to the correct size.
    tokens = realloc(tokens, (n_tokens + 1) * sizeof *tokens);
    assert(tokens != NULL);

    return tokens;
}


//
// Free an array of strings as returned by `tokenize'.
//
static void free_tokens(char **tokens)
{
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);
}

// Open shuck_history file with read mode
static FILE *read_shuck_hist() {
    char shuck_hist[MAX_LINE_CHARS];
    get_shuck_hist_path(shuck_hist);
    FILE *f = fopen(shuck_hist, "r");
    return f;
}

// Execute nth command from shuck_history
static void execute_nth_command(FILE *f, int n, char **path, char **env) {
    // nth command from history
    char *command = find_nth_history(f, n);

    // If given n, is greater than number in history
    if (command == NULL) {
        fprintf(stderr, "!: invalid history reference\n");
        return;
    }

    fprintf(stdout, "%s", command);

    // Tokenise the command
    char **last_words = 
        tokenize(command, (char *) WORD_SEPARATORS, (char *) SPECIAL_CHARS);
    
    execute_command(last_words, path, env);

    free_tokens(last_words);
    free(command);
}

// Check if given word is a valid integer
static int is_integer(char *word) {
    char *w = word;
    int n = strtol(w, &w, 10);

    // Check if given word is a positive integer
    if (*w != '\0' || n < 0) {
        fprintf(stderr, "history: %s: numeric argument required\n", word);
        return -1;
    }

    return n; 
}

// Validate command given
static int validate(char **glob_words) {
    int valid = 1;
    if (valid_input_redir(glob_words) ||
        valid_output_redir(glob_words) ||
        valid_pipes(glob_words) ||
        valid_io(glob_words)) {
        valid = 0;
        if (!valid) {
            return 1;
        }
    }
    return 0;
}

// Check if program can be executed
// Returns 1 if run_program is successfully finishes
// Returns 0 if program is not found
static int check_program(char **glob_words, char **path, char **env, 
                         char *program, char *input_file) {
    // Check if relative path
    if (strstr(program, "/") && is_executable(program)) {
        return run_program(program, env, glob_words, path, input_file);
    }
    // Search through paths to find if given program
    // is executable
    else if (strstr(program, "./") == NULL) {
        char pathname[MAX_LINE_CHARS];
        // Check for possible paths with the program that
        // may be executable
        if (executable_path(program, path, pathname)) {
            return run_program(pathname, env, glob_words, path, input_file);
        }
    }
    return 0;
}