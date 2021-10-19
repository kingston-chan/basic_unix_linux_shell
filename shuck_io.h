// Function that handles the input and output redirection
// of processes that is given from the shell. Also configures
// the standard output of one program to the standard input
// of another program

#include <spawn.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "shuck_helper.h"


// Run the program given by spawning a child process, also handles
// the input and output of the given program
int run_program(char *pathname, char **env, char **glob_words, 
                char **path, char *input_file);
