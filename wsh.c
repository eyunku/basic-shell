#include "wsh.h"

int runargs(int nargs, char *args[], int maxArgs) {
    // check for built-in commands
    if (!strcmp(args[0], "exit")) {
        free(line);
        for (int i = 0; i < maxArgs; i++) free(args[i]);
        free(args);
        return 1;
    } else if (!strcmp(args[0], "cd")) {
        if (nargs == 0 || nargs > 1 || chdir(args[1])) {
            free(line); for (int i = 0; i < maxArgs; i++) free(args[i]); free(args);
            return -1;
        }
    } else if (!strcmp(args[0], "jobs")) {

    } else if (!strcmp(args[0], "fg")) {
        if (nargs != 0 && nargs != 1) {
            free(line); for (int i = 0; i < maxArgs; i++) free(args[i]); free(args);
            return -1;
        }
        
    } else if (!strcmp(args[0], "bg")) {

    } else { // no build-in command found, search bin
        // create executable path
        char exepath[BUFFER_SIZE] = "/bin/";
        strcat(exepath, args[0]);
        
        if (access(exepath, X_OK) < 0) {
            printf("No execute permission for %s\n", args[0]);
            free(line); for (int i = 0; i < maxArgs; i++) free(args[i]); free(args);
            return -1;
        }

        // fork into a child process
        pid_t pid = fork();
        if (pid < 0) {
            printf("Unable to create child process\n");
            free(line); for (int i = 0; i < maxArgs; i++) free(args[i]); free(args);
            return -1;
        }

        // if child process successfully created, check if process is parent or child
        if (pid == 0) { // we're the child process!
            // execvp wipes memory, so we don't need to free line or args
            if (execvp(args[0], args) < 0) {
                printf("Error executing %s\n", args[0]);
                return -1;
            }
        } else { // we're the parent process
            int stat;
            wait(&stat);
            // exit with error if child process call fails
            if (WEXITSTATUS(stat) != 0) {
                free(line); for (int i = 0; i < maxArgs; i++) free(args[i]); free(args);
                return -1;
            }
        }
    }
}

/**
 * Reads a single line from a stream and parses
 * the line into an arg array char* argv
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
    for (int i = 0; i < MAX_ARGS; i++) args[i] = calloc(1, BUFFER_SIZE * sizeof(char));
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
                args[arg] = calloc(1, BUFFER_SIZE * sizeof(char));
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

    // test print to show what parsed cmd & args are
    for (int i = 0; i < maxArgs; i++) {
        printf("arg %d is: %s\n", i, args[i]);
    }
    printf("nargs: %d\n", nargs);

    int err;
    if ((err = runargs(nargs, args, maxArgs)) == 1) {
        free(line);
        return 1;
    } else if (err == -1) {
        free(line);
        return -1;
    }
    // free all line parsing variables (must be freed on any return)
    free(line); for (int i = 0; i < maxArgs; i++) free(args[i]); free(args);
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