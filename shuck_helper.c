#include "shuck_helper.h"

#define APPEND 2
#define OVERWRITE 1
#define MAX_CHARS 1024

// Helper functions to shuck_helper functions
static int invalid_input();
static int invalid_output();
static int invalid_pipes();
static int io_error(char *program);
static int check_builtin_command(char *program);
static int check_io_command(char *program);


// FUNCTIONS FOR SHUCK_HELPER

// Initialise an array with all expanded patterns
char **init_glob_words(char **words) {
    glob_t pattern;
    int k = 0;
    int words_size = array_size(words);
    int glob_size = words_size+1;

    // Create an array size of words to hold the expanded patterns
    char **glob_words = malloc((glob_size)*sizeof(*glob_words));
    for (int j = 0; j < words_size; j++) {
        // Create a buffer to hold the word
        char line[MAX_CHARS];
        strcpy(line, words[j]);
        // Glob will create an array of expanded words if a pattern is 
        // detected
        int pattern_result = glob(line, GLOB_NOCHECK|GLOB_TILDE, NULL,
                                &pattern);
        if (pattern_result != 0) {
            perror("");
            return NULL;
        } else {
            int patt_argc = (int)pattern.gl_pathc;
            // Check if words are expanded, if so need to malloc more 
            // memory for the array
            if (patt_argc > 1) {
                glob_size += patt_argc;
                glob_words = realloc(glob_words, glob_size*sizeof(*glob_words)); 
            }
            for (int t = 0; t < patt_argc; t++) {
                // Need to create a duplicate of the word since it will be freed
                glob_words[k] = strdup(pattern.gl_pathv[t]);
                k++;
            }
        }
        globfree(&pattern);
    }

    glob_words[k] = NULL;

    return glob_words;
}

// Free all memory allocated to char array
void free_array(char **array) {
    if (array == NULL) return;
    for (int i = 0; array[i] != NULL; i++) {
        free(array[i]);
    } 
    free(array);
}

// Returns char array size
int array_size(char **array) {
    int size = 0;
    for (int i = 0; array[i] != NULL; i++) {
        size++;
    } 
    return size;
}

// Concatenate the given path and program
void get_pathname(char *pathname, char *program, char *path) {
    strcpy(pathname, path);
    strcat(pathname, "/");
    strcat(pathname, program);
}

// Validate input redirection command 
int valid_input_redir(char **glob_words) {
    int num_input_redir = 0;
    int input_found = 0;
    int i = 0;

    for (; glob_words[i] != NULL; i++) { 
        // Found a input redirection symbol
        if (!strcmp(glob_words[i], "<")) {
            input_found = 1;
            num_input_redir++;
            // Check if input redirection is at the start
            // of the command
            if (i != 0) {
                return invalid_input();
            }
            // Check if there exists only one input redirection
            // in the command
            else if (num_input_redir > 1) {
                return invalid_input();
            }
            // Check if next word is not an IO redirection
            else if (!strcmp(glob_words[i+1], "|") || 
                     !strcmp(glob_words[i+1], ">")) {
                return invalid_input();
            }
        }
    }
    // If command contains a input redirection, check if there
    // are more than three arguments, since it is required for 
    // input redirection
    if (i < 3 && input_found) {
        return invalid_input();
    }

    return 0;
}

// Validate output redirection command
int valid_output_redir(char **glob_words) {
    int num_output_redir = 0;
    int output_method = 0;
    for (int i = 0; glob_words[i] != NULL; i++) {
        // Found an output redirection symbol
        if (!strcmp(glob_words[i], ">")) {
            num_output_redir++;
            // No output file
            if (glob_words[i+1] == NULL) {
                return invalid_output();
            }
            // Cannot exist at start of command
            else if (i == 0) {
                return invalid_output();
            }
            // Output redirection occurs more than once
            else if (num_output_redir > 1 && output_method == OVERWRITE) {
                return invalid_output();
            }
            // Output redirection occurs more than twice given that an
            // append redirection was found
            else if (num_output_redir > 2 && output_method == APPEND) {
                return invalid_output();
            }
            // Found the first occurence input redirection is append 
            else if (!strcmp(glob_words[i+1], ">") && output_method == 0) {
                output_method = APPEND;
            }
            // First occurence of input redirection is overwrite
            else if (output_method == 0) {
                output_method = OVERWRITE;
            }
            // Check if next word is not a pipe
            else if (!strcmp(glob_words[i+1], "|")) {
                return invalid_output();
            }
        }
    }
    return 0;
}

// Validate pipe command(s)
int valid_pipes(char **glob_words) {
    for (int i = 0; glob_words[i] != NULL; i++) {
        if (!strcmp(glob_words[i], "|")) {
            // Pipe cannot be the first word
            if (i == 0) {
                return invalid_pipes();
            }
            // Next word cannot be null
            else if (glob_words[i+1] == NULL) {
                return invalid_pipes();
            }
            // Next word cannot be an IO redirection command
            else if (!strcmp(glob_words[i+1], ">") ||
                     !strcmp(glob_words[i+1], "|")) {
                return invalid_pipes();
            }
        }
    }
    return 0;
}


// Validate IO and builtin commands
int valid_io(char **glob_words) {
    int i = 0;
    int io_exists = 0;
    // If input redirection exists, skip filename
    if (!strcmp(glob_words[i], "<")) {
        i = 2;
        io_exists = 1;
    } 
    // Pointer to first command
    char *command = glob_words[i];
    for (; glob_words[i] != NULL; i++) {
        // If found a pipe in command, check current
        // command and if it is a builtin command 
        // return an error
        if (!strcmp(glob_words[i], "|")) {
            io_exists = 1;
            if (check_builtin_command(command)) {
                return io_error(command);
            }
            else {
                command = glob_words[i+1];
            }
        }
        // Same goes for output redirection instead
        // we end loop since we don't need to check
        // after it
        else if (!strcmp(glob_words[i], ">")) {
            io_exists = 1;
            if (check_builtin_command(command)) {
                return io_error(command);
            }
            else {
                break;
            }
        }
    }
    // Need to check if first command is a builtin command
    // if there are no pipes or output redirection
    if (io_exists) {
        if (check_builtin_command(command)) {
            return io_error(command);
        }
    }
    return 0;
}

//
// Check whether this process can execute a file.  This function will be
// useful while searching through the list of directories in the path to
// find an executable file.
//
int is_executable(char *pathname) {
    struct stat s;
    return
        // does the file exist?
        stat(pathname, &s) == 0 &&
        // is the file a regular file?
        S_ISREG(s.st_mode) &&
        // can we execute it?
        faccessat(AT_FDCWD, pathname, X_OK, AT_EACCESS) == 0;
}


// Find the full path that the program is executable
int executable_path(char *program, char **path, char *pathname) {
    for (int i = 0; path[i] != NULL; i++) {
        // Get the full pathname including the program
        get_pathname(pathname, program, path[i]);

        if (is_executable(pathname)) {
            return 1;
        }
    }
    return 0;
}

// HELPER FUNCTIONS FOR ABOVE FUNCTIONS

// Returns stderr outputs for respective errors
static int invalid_input() {
    fprintf(stderr, "invalid input redirection\n");
    return 1;
}

static int invalid_output() {
    fprintf(stderr, "invalid output redirection\n");
    return 1;
}

static int invalid_pipes() {
    fprintf(stderr, "invalid pipe\n");
    return 1;
}

static int io_error(char *program) {
    fprintf(stderr, 
            "%s: I/O redirection not permitted for builtin commands\n", 
            program);
    return 1;
}

// Check if word is a builtin command
static int check_builtin_command(char *program) {
    if (strcmp(program, "history") == 0) {
        return 1;
    } else if (strcmp(program, "!") == 0) {
        return 1;
    } else if (strcmp(program, "pwd") == 0) {
        return 1;
    } else if (strcmp(program, "cd") == 0) {
        return 1;
    }
    return 0;
}

// Check if word is an IO command
static int check_io_command(char *program) {
    if (strcmp(program, "<") == 0) {
        return 1;
    } else if (strcmp(program, "|") == 0) {
        return 1;
    } else if (strcmp(program, ">") == 0) {
        return 1;
    }
    return 0;
}

