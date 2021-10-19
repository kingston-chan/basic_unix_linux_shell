#include "shuck_io.h"

#define OVERWRITE 1
#define APPEND 2
#define MAX_CHARS 1024

// Helper function
static int output_redirection_exists(char **glob_words, int *output_index);
static int pipes_exist(char **glob_words);
static int pipelines(char **glob_words, char **path, char **env, 
                     int *rfd, int *wfd, int num_pipes);
static int close_pipe(posix_spawn_file_actions_t *a, int fd);
static int pipe_to_stdout(posix_spawn_file_actions_t *a, int fd);
static int pipe_to_stdin(posix_spawn_file_actions_t *a, int fd);
static char *process_exec(char *process, char **path);
static void free_args(char ***args, int size);
static char *init_args_pathnames(char ***args, char **pathnames, 
                                 char **glob_words, int *output_idx, 
                                 int num_process, char **path);


// Run program
// Successfully ran program = 1
// Encountered error = 2
// Program not executable = 0 (Mostly for pipes)
int run_program(char *pathname, char **env, char **glob_words, char **path, 
                char *input_file) {

    // Create file descriptors for read and write
    int read_fd = 0;
    int write_fd = 0;
    int read_exists = 0;

    // Initialise the read file descriptor and connect it
    // to the program's standard input 
    if (!strcmp(glob_words[0], "<")) {
        read_exists = 1;
        read_fd = open(input_file, O_RDONLY);
        if (read_fd == -1) {
            perror("open");
            return 2;
        }
        
    }

    int output_index = 0;
    int output_exists = output_redirection_exists(glob_words, &output_index);
    if (output_exists) {
        if (output_exists == APPEND) {
            // File descriptor appends to file
            write_fd = open(glob_words[output_index+1], O_CREAT|O_WRONLY|O_APPEND, 0644);
        } else {
            // File descriptor overwrites file
            write_fd = open(glob_words[output_index+1], O_CREAT|O_WRONLY|O_TRUNC, 0644);
        }
        if (write_fd == -1) {
            perror("open");
            return 2;
        }
    }

    // Check if there are pipes in the command
    int num_pipes = pipes_exist(glob_words);
    if (num_pipes) {
        // Configure pipelines for the programs/processes
        return pipelines(glob_words, path, env, &read_fd, &write_fd, num_pipes);
    }


    posix_spawn_file_actions_t actions;

    if (posix_spawn_file_actions_init(&actions) != 0) {
        perror("posix_spawn_file_actions_init");
        return 2;
    }

    // Connect read file descriptor to standard input of program
    if (read_exists) {
        if (posix_spawn_file_actions_adddup2(&actions, read_fd, 0) != 0) {
            perror("posix_spawn_file_actions_adddup2");
            return 2;
        }
    }

    // Connect write file descriptor to standard output of program
    if (output_exists) {
        if (posix_spawn_file_actions_adddup2(&actions, write_fd, 1) != 0) {
            perror("posix_spawn_file_actions_adddup2");
            return 2;
        }
    }

    // Create a NULL terminated char array to hold the program and its 
    // arguments
    char **args = malloc((array_size(glob_words)+1)*sizeof(*args));
    int j = 0;
    int i = 0;
    // If input redirection exists, skip the filename
    if (!strcmp(glob_words[i], "<")) {
        i += 2;
    }
    for (; glob_words[i] != NULL; i++) {
        // If output redirection, all arguments have been copied
        if (!strcmp(glob_words[i], ">")) {
            break;
        }
        else {
            args[j] = glob_words[i];
            j++;
        }
    }
    args[j] = NULL; 

    pid_t pid;

    if (posix_spawn(&pid, pathname, &actions, NULL, args, env) != 0) {
        perror("spawn");
        return 2;
    }

    // Close the unused file descriptors
    if (read_fd != 0) close(read_fd);
    if (write_fd != 0) close(write_fd);

    // Wait for child process to finish execution
    int exit_status;
    if (waitpid(pid, &exit_status, 0) == -1) {
        perror("waitpid");
        return 2;
    }
    
    fprintf(stdout, "%s exit status = %d\n", pathname, 
            WEXITSTATUS(exit_status));

    // Free allocated memory
    posix_spawn_file_actions_destroy(&actions);

    free(args);

    return 1;
}


// Links the input and output of child processes through pipes
// Return 1 if successfully create pipelines between child processes
// and executed it.
// Returns 2 if an error is encountered
static int pipelines(char **glob_words, char **path, char **env, 
                     int *rfd, int *wfd, int num_pipes) {
    // Each pipe connects two processes
    int num_process = num_pipes+1;
    
    // Create an array to hold the pathnames
    char **pathnames = malloc((num_process+1)*sizeof(*pathnames));
    // Create an array of array of words to hold the program and its 
    // arguments
    char ***args = malloc(num_process*sizeof(**args));
    int output_idx = 0;

    char *proc = init_args_pathnames(args, pathnames, 
                                     glob_words, &output_idx, 
                                     num_process, path);
    if (proc != NULL) 
    {
        // One of the processes is not executable
        fprintf(stderr, "%s: command not found\n", proc);
        return 2;
    }

    // Initialise the pipes
    int **fd = malloc(num_pipes*sizeof(*fd));

    for (int i = 0; i < num_pipes; i++) {
        fd[i] = malloc(2*sizeof(int));

        if (pipe(fd[i]) == -1) {
            perror("pipe");
            return 2;
        }
    }
    // Create an array of pids for the child processes
    pid_t *pid = malloc(num_process*sizeof(*pid));

    // Create an array of file actions to manage the pipes of the child processes
    posix_spawn_file_actions_t *actions = malloc(num_process*sizeof(*actions));

    // Execute the child processes and configure the pipes
    for (int i = 0; i < num_process; i++) {
        if (posix_spawn_file_actions_init(&actions[i]) != 0) {
            perror("posix_spawn_file_actions_init");
            return 2;
        }
        if (i == 0 && *rfd != 0) {
            printf("%d\n", *rfd);
            if (posix_spawn_file_actions_adddup2(&actions[i], *rfd, 0) != 0) {
                perror("posix_spawn_file_actions_adddup2");
                return 2;
            }
        } 
        // Close and create stdin/stdout respective pipes for each
        // process
        // Will return 2 if encountered an error with opening and
        // closing pipes
        for (int j = 0; j < num_pipes; j++) {
            if (i == 0) {
                // First process only needs its standard output
                // to be connected to the first pipe's write end
                // Close all the other unused pipes
                if (close_pipe(&actions[i], fd[j][0])) {
                    return 2;
                }
                if (j == 0) {
                    if (pipe_to_stdout(&actions[i], fd[j][1])) {
                        return 2;
                    }
                }
                if (close_pipe(&actions[i], fd[j][1])) {
                    return 2;
                }
            } else {
                // For other processes connect to the previous pipes
                // read end for standard input and the connect its standard
                // output for the next pipe's write end
                // Close pipes that are not used by child process
                if (j == i-1) {
                    if (pipe_to_stdin(&actions[i], fd[j][0])) {
                        return 0;
                    }
                }
                if (j+1 >= i) {
                    if (close_pipe(&actions[i], fd[j][0])) {
                        return 2;
                    }
                }
                if (j == i) {
                    if (pipe_to_stdout(&actions[i], fd[j][1])) {
                        return 2;
                    }
                }
                if (j >= i) {
                    if (close_pipe(&actions[i], fd[j][1])) {
                        return 2;
                    }
                }
                // The last process will connect its output to the 
                // write file descriptor, for output redirection if 
                // it exists
                if (i == num_process-1 && *wfd != 0) {
                    printf("%d\n", *wfd); 
                    if (posix_spawn_file_actions_adddup2(&actions[i], *wfd, 1)
                                                        != 0)
                    {
                        perror("posix_spawn_file_actions_adddup2");
                        return 2;
                    }
                }
            }
        }

        if (posix_spawn(&pid[i], pathnames[i], &actions[i], NULL, args[i], 
                        env) != 0) {
            fprintf(stderr, "%s\n", pathnames[i]);
            perror("spawn");
            return 2;
        }

        // Need to close the pipes that were just used, since we don't need
        // it for the next child processes
        if (i > 0) close(fd[i-1][0]);
        if (i < num_pipes) close(fd[i][1]);
        // Close unused file descriptors
        if (i == 0 && *rfd != 0)  close(*rfd);
        if (i == num_process-1 && *wfd != 0)  close(*wfd);
    }

    int final_exit_status;
    // Need to wait for all the child processes to finish executing
    for (int i = 0; i < num_process; i++) {
        int exit_status;

        if (waitpid(pid[i], &exit_status, 0) == -1) {
            perror("waitpid");
            return 0;
        }

        if (i == num_process-1) final_exit_status = exit_status;
    }

    fprintf(stdout, "%s exit status = %d\n", pathnames[num_process-1], 
            WEXITSTATUS(final_exit_status));
    
    // Free all the allocated memory (arrays)

    for(int i = 0; i < num_process; i++) {
        posix_spawn_file_actions_destroy(&actions[i]);
    }
    free(actions);
    free(pid);

    free_array(pathnames);
    free_args(args, num_process);

    for(int i = 0; i < num_pipes; i++) {
        free(fd[i]);
    }
    free(fd);

    return 1;
}


// Check if output redirection exists, stores the output index and 
// returns the write to file method
static int output_redirection_exists(char **glob_words, int *output_index) {
    int exists = 0;
    for (int i = 0; glob_words[i] != NULL; i++) {
        if (!strcmp(glob_words[i], ">")) {
            if (!strcmp(glob_words[i+1], ">")) {
                exists = APPEND;
                *output_index = i+1;
            }
            else {
                exists = OVERWRITE;
                *output_index = i;
            }
            return exists;
        }
    }
    return exists;
}


// Check if pipes exist, returns the number of pipes
static int pipes_exist(char **glob_words) {
    int pipes = 0;
    for (int i = 0; glob_words[i] != NULL; i++) {
        if (!strcmp(glob_words[i], "|")) pipes++;
    }
    return pipes;
}

// Close child process's read/write end of pipe(s)
static int close_pipe(posix_spawn_file_actions_t *a, int fd) {
    if (posix_spawn_file_actions_addclose(a, fd) != 0) {
        perror("posix_spawn_file_actions_addclose");
        return 1;
    }
    return 0;
}

// Connect a pipe to the standard output of a child process
static int pipe_to_stdout(posix_spawn_file_actions_t *a, int fd) {
    if (posix_spawn_file_actions_adddup2(a, fd, 1) != 0) {
        perror("posix_spawn_file_actions_adddup2");
        return 1;
    }
    return 0;
}

// Connect a pipe to the standard intput of a child process
static int pipe_to_stdin(posix_spawn_file_actions_t *a, int fd) {
    if (posix_spawn_file_actions_adddup2(a, fd, 0) != 0) {
        perror("posix_spawn_file_actions_adddup2");
        return 1;
    }
    return 0;
}

// Check if process will executable
static char *process_exec(char *process, char **path) {
    char *process_path = NULL;
    // Check if relative path
    if (strstr(process, "/") && is_executable(process)) {
        process_path = strdup(process);
        return process_path;
    }
    // Search through paths to find if given process
    // is executable
    else if (strstr(process, "./") == NULL) {
        char pathname[MAX_CHARS];
        // Check for possible paths with the process that
        // may be executable
        if (executable_path(process, path, pathname)) {
            process_path = strdup(pathname);
            return process_path;
        }
    }
    // Process is not executable
    return NULL;
}

// Free the array of array of words
static void free_args(char ***args, int size) {
    for (int i = 0; i < size; i++) {
        free(args[i]);
    }
    free(args);
}

// Initialise the array of array of programs and arguments, array of pathnames,
// and find if there is a output redirection
static char *init_args_pathnames(char ***args, char **pathnames, 
                                 char **glob_words, int *output_idx, 
                                 int num_process, char **path) 
{
    int glob_index = 0;
    // Ignore the input redirection
    if (!strcmp(glob_words[glob_index], "<")) glob_index = 2;
    for (int i = 0; i < num_process; i++) {
        int max_size = 3;
        int size = 0;
        int k = 0;

        args[i] = malloc(max_size*sizeof(*args[i]));
        
        while (glob_words[glob_index] != NULL) {
            // Reach a pipe we know its end of arguments
            if (!strcmp(glob_words[glob_index], "|")) {
                glob_index++;
                break;
            }
            // Reach a output redirection we also know it is 
            // end of arguments
            if (!strcmp(glob_words[glob_index], ">")) {
                *output_idx = glob_index;
                break;
            }

            // At process, need to check if executable, if so
            // save the pathnames and program name
            if (k == 0) {
                pathnames[i] = process_exec(glob_words[glob_index], path);

                if (pathnames[i] != NULL) {
                     args[i][k] = glob_words[glob_index];
                } 
                else {
                    // Process cannot be executed
                    free_args(args, num_process);
                    free_array(pathnames);
                    return glob_words[glob_index];
                }
            }
            else {
                if (size == max_size) {
                    // Double the array size for storing program and its arguments
                    max_size *= 2;
                    args[i] = realloc(args[i], max_size*sizeof(*args[i]));
                }
                args[i][k] = glob_words[glob_index];
            }
            k++;
            glob_index++;
            size++;
        }
        // Make room for NULL terminator of the array
        if (size == max_size) {
            args[i] = realloc(args[i], (max_size+1)*sizeof(*args[i]));
        }
        args[i][k] = NULL;
    }
    pathnames[num_process] = NULL;
    return NULL;
}

