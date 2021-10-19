#include "shuck_builtins.h"

#define MAX_CHARS 1024
#define LAST_COMMAND -1

// Helper functions
static int num_file_lines(FILE *f);

// Change the directory
int change_directory(char **glob_words) {
    char *directory = glob_words[1];

    // Check if there are too many arguments
    // If not, change to given directory given 
    // that it is valid. If no arguments or argu, change to
    // $HOME directory
    if (array_size(glob_words) > 2) {
        fprintf(stderr, "cd: too many arguments\n");
        return 0;
    } 
    else if (directory == NULL) {
        if (chdir(getenv("HOME")) != 0) {
            perror("");
            return 0;
        }
    } 
    else {
        if (chdir(glob_words[1]) != 0) {
            fprintf(stderr, "cd: %s: ", glob_words[1]);
            perror("");
            return 0;
        }
    }
    return 1;
}

// Print the current directory
int current_directory(char **glob_words) {
    // Check if valid arguments
    if (array_size(glob_words) > 1) {
        fprintf(stderr, "pwd: too many arguments\n");
        return 0;
    }
    // Get the current directory
    char pathname[MAX_CHARS];
    if (getcwd(pathname, sizeof(pathname)) == NULL) {
        perror("getcwd");
        return 0;
    }
    fprintf(stdout, "current directory is \'%s\'\n", pathname);
    return 1;
}

// Find the nth history command in shuck_history
// file and return it
char *find_nth_history(FILE *f, int n) {
    // Find last line in command history
    if (n == LAST_COMMAND) {
        int num_lines = num_file_lines(f);
        n = num_lines-1;
        // Move file pointer to start of file
        fseek(f, 0, SEEK_SET);
    }
    
    int i = 0;
    char *command = NULL;

    // Find nth command in file
    char line[MAX_CHARS];
    while (fgets(line, sizeof line, f) != NULL) {
        if (i == n) {
            command = strdup(line);
        }
        i++;
    }

    return command;
}

// Print out last nth commands in shuck_history
// file
void print_nth_history(FILE *f, int n) {
    // Get number of lines in shuck_history file
    int num_lines = num_file_lines(f);
    // Seek back to start of the file, since file 
    // pointer is at end of file from getting the 
    // number of lines
    fseek(f, 0, SEEK_SET);
    // Limit the max number of lines printed out
    // if given n is more than number of lines in 
    // the file
    if (n >= num_lines) n = num_lines;
    int print_lines = num_lines - n;
    int i = 0;

    char line[MAX_CHARS];
    while (fgets(line, sizeof line, f) != NULL) {
        if (i >= print_lines) {
            fprintf(stdout, "%d: %s", i, line);
        }
        i++;
    }

    return;
}


// Add given words array to shuck_history file
void add_to_history(char **words) {
    char shuck_hist[MAX_CHARS];
    get_shuck_hist_path(shuck_hist);

    FILE *f = fopen(shuck_hist, "a");
    if (f == NULL) {
        perror("fopen");
        return;
    }
    
    // Concatenate the words into single line
    // to append to file
    char arg[MAX_CHARS];
    strcpy(arg, words[0]);
    for (int i = 1; words[i] != NULL; i++) {
        strcat(arg, " ");
        strcat(arg, words[i]);
    }

    fprintf(f, "%s\n", arg);
    fclose(f);
    return;
}

// Get the shuck_history path
void get_shuck_hist_path(char *shuck_hist) {
    char *home = getenv("HOME");
    strcpy(shuck_hist, home);
    strcat(shuck_hist, "/");
    strcat(shuck_hist, ".shuck_history");
}

// Get number of lines in file
static int num_file_lines(FILE *f) {
    int lines = 0;
    char line[MAX_CHARS];
    while (fgets(line, sizeof line, f) != NULL) {
        lines++;
    }
    return lines;
}
