#include "wsh.h"

// because it is unclear whether a child process should exit the whole shell or not
// control the exit value here (0 for exit child only, -1 for exit shell)
#define EXIT_CHILD 0

/**
 * Executes a single command with nargs arguments
 * 
 * Parameters:
 *  nargs (int): number of arguments
 *  args (char*[]): arguments, as an array of strings.
 *                  first arg is the cmd.
 * 
 * Returns:
 *  int: -1 on error, 1 if shell should be clean exited, 0 otherwise
 *          special case: 2 if output was written to
*/
int runargs(int nargs, char *args[], char* output) {
    // check for built-in commands
    if (!strcmp(args[0], "exit")) {
        return 1;
    } else if (!strcmp(args[0], "cd")) {
        if (nargs == 0 || nargs > 1 || chdir(args[1])) {
            return EXIT_CHILD;
        }
    } else if (!strcmp(args[0], "jobs")) {

    } else if (!strcmp(args[0], "fg")) {
        if (nargs != 0 && nargs != 1) {
            return EXIT_CHILD;
        }

    } else if (!strcmp(args[0], "bg")) {

    } else { // no build-in command found, search bin
        // create executable path
        char exepath[BUFFER_SIZE] = "/bin/";
        strcat(exepath, args[0]);

        if (access(exepath, X_OK) < 0) {
            printf("No execute permission for %s\n", args[0]);
            return EXIT_CHILD;
        }

        // fork into a child process
        pid_t pid = fork();
        if (pid < 0) {
            printf("Unable to create child process\n");
            return EXIT_CHILD;
        }

        // if child process successfully created, check if process is parent or child
        if (pid == 0) { // we're the child process!
            // execvp wipes memory, so we don't need to free line or args
            if (execvp(args[0], args) < 0) {
                printf("Error executing %s\n", args[0]);
                return EXIT_CHILD;
            }
        } else { // we're the parent process
            int stat;
            wait(&stat);
            // exit with error if child process call fails
            if (WEXITSTATUS(stat) != 0) {
                return EXIT_CHILD;
            }
        }
    }
    return 0;
}

/**
 * Reads a single line from a stream and parses
 * the line into an arg array char* argv
 * 
 * Parameters:
 *  stream (FILE*): filestream to read from
 * 
 * Returns:
 *  int: -1 on error, 1 if shell should be clean exited, and 0 otherwise
*/
int readline(FILE* stream) {
    char *line = NULL;
    size_t len = 0;
    ssize_t linelen;
    if ((linelen = getline(&line, &len, stream)) < 0) {
        // EOF reached
        free(line);
        return 1;
    } else if (linelen <= 1) { // ignore lines with just \n or empty
        return 0;
    }
    
    // parse line into command & args
    /** NOTE: execvp expects char *const argv[], where
     * argv[0] contains the command name,
     * each arg is a string (i.e. terminated by \0)
     * the whole array is terminated with NULL (argv[nargs + 1] = NULL)
    **/
    int maxArgs = MAX_ARGS;
    char **args = calloc(MAX_ARGS, sizeof(char*));
    for (int i = 0; i < MAX_ARGS; i++) args[i] = calloc(BUFFER_SIZE, sizeof(char));
    int nargs = 0;
    int counter = 0; // iterates through cmd/args
    for (int i = 0; i < linelen; i++) {
        // double size of args array if no space left
        if (nargs >= maxArgs) {
            char temp[maxArgs][BUFFER_SIZE];
            for (int arg = 0; arg < nargs; arg++) {
                strcpy(temp[arg], args[arg]);
                free(args[arg]);
            }
            free(args);
            args = calloc(maxArgs *= 2, sizeof(char*));
            for (int arg = 0; arg <= maxArgs; arg++) {
                args[arg] = calloc(BUFFER_SIZE, sizeof(char));
                if (arg < nargs) strcpy(args[arg], temp[arg]);
            }
        }
        if (line[i] == ' ' || line[i] == '\n') {
            // convert to string
            args[nargs][counter] = '\0';

            // break on newline, since end of line reached
            if (line[i] == '\n') break;

            // go to next arg, reset cmd/arg iterator
            nargs++;
            counter = 0;
            continue;
        }
        
        args[nargs][counter++] = line[i];
    }
    args[nargs + 1] = NULL;

    // TODO: remove
    // test print to show what parsed cmd & args are
    // for (int i = 0; i < maxArgs; i++) {
    //     printf("arg %d is: %s\n", i, args[i]);
    // }

    // parse pipe characters so that args can be piped
    int npipe = 0;
    for (int i = 0; i < nargs; i++) { if (!strcmp(args[i], "|")) npipe++; }
    char ***cmds = calloc(npipe + 1, sizeof(char**)); // # of cmds will always be # of pipes + 1
    for (int i = 0; i < npipe + 1; i++) {
        // maximum # of args is nargs, +1 cmd, +1 for array size
        cmds[i] = calloc(nargs + 2, sizeof(char*));
        for (int j = 0; j < nargs + 2; j++) {
            cmds[i][j] = calloc(BUFFER_SIZE, sizeof(char));
        }
    }
    
    int arg = 0; // index of arg from args
    int cmd = 0; // which cmd we're on
    int cmdArg = 0; // which arg of this cmd we're on
    while (args[arg] != NULL) {
        //if (args[arg + 1] == NULL) printf("next is null\n");
        if (!strcmp(args[arg], "|")) {
            if (arg == nargs) { // last arg is a pipe
                printf("Pipe has no target\n");
                free(line); 
                for (int i = 0; i < maxArgs; i++) free(args[i]);
                free(args);
                for (int i = 0; i < npipe + 1; i++) {
                    free(cmds[i]);
                    for (int j = 0; j < nargs; j++) {
                        free(cmds[i][j]);
                    }
                }
                free(cmds);
                return -1;
            }
            cmds[cmd++][0][0] = cmdArg; // store # of args for cmd in 0 index
            cmdArg = 0;
            arg++;
        }
        // reserve cmds[cmd][0] for # of args for that command
        strcpy(cmds[cmd][1 + cmdArg++], args[arg++]);
    }
    // store # of args for last cmd (won't have pipe)
    cmds[cmd++][0][0] = cmdArg;

    // TODO: remove
    // test print to see if pipes are being parsed properly
    // for (int i = 0; i < npipe + 1; i++) {
    //     for (int j = 1; j <= cmds[i][0][0]; j++) {
    //         printf("(%d) arg %d is: %s\n", i, j, cmds[i][j]);
    //     }
    // }

    // free all line parsing variables
    free(line); 
    for (int i = 0; i < maxArgs; i++) free(args[i]);
    free(args);
    for (int i = 0; i < npipe + 1; i++) {
        for (int j = 0; j < nargs + 2; j++) {
            free(cmds[i][j]);
        }
        free(cmds[i]);
    }
    free(cmds);

    return 0;
}

int interactive(void) {
    // print initial prompt
    printf("wsh> ");

    while (1) {
        int err;
        if ((err = readline(stdin)) < 0) return -1;
        else if (err == 1) return 0;
        printf("wsh> ");
    }

    return 0;
}

int batch(char* filename) {
    FILE *fp;
    if ((fp = fopen(filename, "r")) == NULL) { printf("Unable to open batch file\n"); return -1; }
    
    while (1) {
        int err;
        if ((err = readline(fp)) < 0) return -1;
        else if (err == 1) return 0;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc == 1) exit(interactive());
    else if (argc == 2) exit(batch(argv[1]));
}