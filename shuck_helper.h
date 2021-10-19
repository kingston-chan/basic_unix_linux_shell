// Helper functions that are called by other functions

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <glob.h>

// Return a copy of given array of words
// such that all patterns are expanded
char **init_glob_words(char **words);

// Free given char array given
// that its last element is NULL
// If given array is NULL, return nothing
void free_array(char **array);

// Get the number of elements in 
// a char array, given that the last element
// is NULL. NULL is excluded
int array_size(char **array);

// Given a path and program, concatenate them
// with a '/' between them
void get_pathname(char *pathname, char *program, char *path);

// Check if command has valid input redirection 
int valid_input_redir(char **glob_words);

// Check if command has valid output redirection
int valid_output_redir(char **glob_words);

// Check if command has valid pipes redirection
int valid_pipes(char **glob_words);

// Since shell does not support IO redirection with builtin
// commands, need to validate that none exists when IO commands
// exist
int valid_io(char **glob_words);

// Check if given pathname is executable 
int is_executable(char *pathname);

// Check if given program is executable by checking if program is an
// executable relative path or it can be executed through a path
int executable_path(char *program, char **path, char *pathname);