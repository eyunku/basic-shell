#include "wsh.h"

// because it is unclear whether a child process should exit the whole shell or not
// control the exit value here (0 for exit child only, -1 for exit shell)
#define EXIT_CHILD 0

/**
 * Spawns a child process and assigns it the process ID of the group it belongs to.
 * Each line passed into the shell should only create a single process with a single
 * gpid. The gpid for the group will be equivalent to the pid of the first child process
 * spawned, that is, the first cmd in the list of cmds.
*/
int runarg(pid_t gpid, int fdin, int fdout, char *args[]) {
    pid_t pid;
    if ((pid = fork()) == 0) { // only true if we're a child process
        if (fdin != fileno(stdin)) {
            dup2(fdin, fileno(stdin));
            close(fdin);
        }

        if (fdout != fileno(stdout)) {
            dup2(fdout, fileno(stdout));
            close(fdout);
        }

        return execvp(args[0], args);
    }
    return pid;
}

/**
 * Executes a single line passed into the shell, after being preprocssed
 * by the readline function
 * 
 * Parameters:
 *  cmds (char**[]): array of commands representing the set of cmds 
 *                   piped into one another, parsed from the input line
 * 
 * Returns:
 *  int: -1 on error, 1 if shell should be clean exited, 0 otherwise
*/
int runargs(int npipe, char **cmds[]) {
    // NOTE: the 0th element of each cmd is the number of arguments 

    // check for built-in commands
    printf("first command passed in is: %s, number of args for first command is %d\n", cmds[0][1], cmds[0][0][0]);
    if (!strcmp(cmds[0][1], "exit")) {
        return 1;
    } else if (!strcmp(cmds[0][1], "cd")) {
        if (cmds[0][0][0] == 0 || cmds[0][0][0] > 1 || chdir(cmds[0][2])) {
            return EXIT_CHILD;
        }
    } else if (!strcmp(cmds[0][1], "jobs")) {

    } else if (!strcmp(cmds[0][1], "fg")) {

    } else if (!strcmp(cmds[0][1], "bg")) {

    } else { // no build-in command found
        pid_t pid; // process id to differentiate from root child and parent (shell)
        pid_t gpid; // group process id, defined using the pid of the first process called
        int fdin = fileno(stdin); // initialize to read from stdin
        int fd[2]; // keep track of fdin and fdout for each process
        
        // create root child process
        if (pipe(fd) < 0) { printf("Pipe creation failed\n"); return EXIT_CHILD; }
        pid = fork();

        // if child process successfully created, check if process is parent or child
        if (pid == 0) { // we're the child process!
            int i;
            gpid = getpid();
            printf("gpid: %d\n", gpid);
            for (i = 0; i < npipe; ++i) {
                if (pipe(fd) < 0) { printf("Pipe creation failed\n"); return EXIT_CHILD; }
                // carry the write in into the read
                runarg(gpid, fdin, fd[1], &(cmds[i][1]));
                // close write end of pipe
                close(fd[1]);
                // keep read end of pipe, next child will read from here
                fdin = fd[0];
            }
            if (fdin != fileno(stdin)) dup2(fdin, fileno(stdin));
            
            // execvp clears memory space, so no frees are needed
            execvp(cmds[i][1], &(cmds[i][1]));
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
        cmds[i] = calloc(maxArgs, sizeof(char*));
        for (int j = 0; j < maxArgs; j++) {
            cmds[i][j] = calloc(BUFFER_SIZE, sizeof(char));
        }
    }

    int arg = 0; // index of arg from args
    int cmd = 0; // which cmd we're on
    int cmdArg = 0; // which arg of this cmd we're on
    printf("%d\n", arg);
    while (args[arg] != NULL) {
        // if (args[arg + 1] == NULL) printf("next is null\n");
        if (!strcmp(args[arg], "|")) {
            if (arg == nargs) { // last arg is a pipe
                printf("Pipe has no target\n");
                free(line); 
                for (int i = 0; i < maxArgs; i++) free(args[i]);
                free(args);
                for (int i = 0; i < npipe + 1; i++) {
                    free(cmds[i]);
                    for (int j = 0; j < maxArgs; j++) {
                        free(cmds[i][j]);
                    }
                }
                free(cmds);
                return -1;
            }
            cmds[cmd][cmdArg + 1] = NULL;
            cmds[cmd++][0][0] = cmdArg - 1; // store # of args for cmd in 0 index
            cmdArg = 0;
            arg++;
        }
        // reserve cmds[cmd][0] for # of args for that command
        strcpy(cmds[cmd][1 + cmdArg++], args[arg++]);
    }
    // write null terminator and number of args for last arg (won't have pipe)
    cmds[cmd][cmdArg + 1] = NULL;
    cmds[cmd++][0][0] = cmdArg - 1;

    // TODO: remove
    // test print to see if pipes are being parsed properly
    // for (int i = 0; i < npipe + 1; i++) {
    //     printf("number of args: %d\n", cmds[i][0][0]);
    //     for (int j = 0; j < maxArgs; j++) {
    //         printf("(%d) arg %d is: %s\n", i, j, cmds[i][j]);
    //     }
    // }

    // call cmd list
    int err = runargs(npipe, cmds);

    // free all line parsing variables
    free(line); 
    for (int i = 0; i < maxArgs; i++) free(args[i]);
    free(args);
    for (int i = 0; i < npipe + 1; i++) {
        for (int j = 0; j < maxArgs; j++) {
            free(cmds[i][j]);
        }
        free(cmds[i]);
    }
    free(cmds);

    return err;
}

/**
 * Creates an interactive shell environment
 * 
 * Returns:
 *  int: exit status
*/
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

/**
 * Runs a batch file through a shell environment
 * 
 * Parameters:
 *  filename (char*): path to batch file
 * 
 * Returns:
 *  int: exit status
*/
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