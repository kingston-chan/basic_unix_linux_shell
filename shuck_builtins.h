// Functions that handle how the builtin commands
// operate (cd, pwd, history and !)

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "shuck_helper.h"

// Change the directory given an directory, if no
// given directory, change to $HOME directory 
int change_directory(char **glob_words);

// Prints the current directory
int current_directory(char **glob_words);

// Find the nth command history in the 
// .shuck_history file. If no nth is
// specified, return the last command
char *find_nth_history(FILE *f, int n);


// Print out the last nth history commands
// If no specified n, it will print out the 
// last DEFAULT_HISTORY_SHOWN
void print_nth_history(FILE *f, int n);


// Append the last issued command in the shell
// to the .shuck_history file
void add_to_history(char **words);


// Get the shuck_history path
void get_shuck_hist_path(char *shuck_hist);