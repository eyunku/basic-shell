#include "wsh.h"

// controls if an failure on a child process exits the shell: 0 if no, -1 if yes
#define EXIT_CHILD 0

// initialize jobs with all null pointers
Job *jobs[MAX_JOBS] = {NULL, };

// pid of shell
pid_t shell;

/**
 * Kills a job with a given id.
 * 
 * Parameters:
 *  id (int): the id of the job
*/
void killJob(int id) {
    for (int i = 0; i < jobs[id]->maxArgs; i++) free(jobs[id]->args[i]);
    free(jobs[id]->args);
    for (int i = 0; i < jobs[id]->npipe + 1; i++) {
        for (int j = 0; j < jobs[id]->maxArgs; j++) free(jobs[id]->cmds[i][j]);
        free(jobs[id]->cmds[i]);
    }
    free(jobs[id]->cmds);
    free(jobs[id]);
    jobs[id] = NULL;
}

/**
 * Check all jobs to see if they've completed. If they have,
 * clear the job from the job array (that is, remove all zombie processes)
*/
void killZombies() {
    for (int i = 1; i < MAX_JOBS; i++) {
        if (jobs[i] == NULL) continue;
        int stat;
        if (!waitpid(-jobs[i]->pgid, &stat, WNOHANG)) { // if return value is 0
            // printf("NOHANG triggered\n");
            continue;
        }
        if (WIFEXITED(stat)) { // job completed
            killJob(jobs[i]->id);
        }
    }
}

/**
 * Empty signal handler so that a process can ignore a signal
*/
void sigHandler() {}

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
int runargs(int npipe, Job *job) {
    // check for built-in commands
    // since built-in commands don't create a process, they immediately become zombies and must be killed
    if (!strcmp(job->cmds[0][1], "exit")) {
        killJob(job->id);
        return 1;
    } else if (!strcmp(job->cmds[0][1], "cd")) {
        if (job->cmds[0][0][0] == 0 || job->cmds[0][0][0] > 1 || chdir(job->cmds[0][2])) {
            killJob(job->id);
            return EXIT_CHILD;
        }
        killJob(job->id);
    } else if (!strcmp(job->cmds[0][1], "jobs")) {
        // printf("running jobs\n");
        for (int i = 1; i < MAX_JOBS; i++) {
            if (i == job->id) continue; // don't print jobs as it runs (it is in fg)
            if (jobs[i] != NULL && !jobs[i]->foreground) {
                printf("%d:", jobs[i]->id);
                int arg = 0;
                while (jobs[i]->args[arg] != NULL) { printf(" %s", jobs[i]->args[arg++]); }
                printf("\n");
            }
        }
        killJob(job->id);
    } else if (!strcmp(job->cmds[0][1], "fg")) {
        if (job->cmds[0][0][0] == 0) { // 0 args
            // find largest job id
            Job *fgJob = NULL;
            for (int i = MAX_JOBS - 1; i > 0; i--) {
                if (jobs[i] != NULL && !jobs[i]->foreground) {
                    fgJob = jobs[i];
                    break;
                }
            }
            if (fgJob == NULL) {
                printf("No jobs found\n");
                killJob(job->id);
                return EXIT_CHILD;
            }
            // continue job and bring to foreground
            kill(-fgJob->pgid, SIGCONT);
            tcsetpgrp(fileno(stdin), fgJob->pgid);
            int stat;
            waitpid(-fgJob->pgid, &stat, WUNTRACED);
            // reassign control of terminal to shell
            tcsetpgrp(fileno(stdin), getpgid(shell));
            fgJob = NULL;
        } else if (job->cmds[0][0][0] == 1) { // 1 args
            // fetch relevant job
            Job *fgJob = jobs[atoi(job->args[1])];
            if (fgJob == NULL) {
                printf("Job does not exist\n");
                killJob(job->id);
                return EXIT_CHILD;
            } else if (fgJob->foreground) {
                printf("Job is already in foreground\n");
                killJob(job->id);
                return EXIT_CHILD;
            }
            // continue job and bring to foreground
            kill(-fgJob->pgid, SIGCONT);
            tcsetpgrp(fileno(stdin), fgJob->pgid);
            int stat;
            waitpid(-fgJob->pgid, &stat, WUNTRACED);
            // reassign control of terminal to shell
            tcsetpgrp(fileno(stdin), getpgid(shell));
            fgJob = NULL;
        } else {
            printf("fg requires either 0 or 1 arg\n");
            killJob(job->id);
            return EXIT_CHILD;
        }
        killJob(job->id);
    } else if (!strcmp(job->cmds[0][1], "bg")) {
        if (job->cmds[0][0][0] == 0) {
            Job *bgJob = NULL;;
            for (int i = MAX_JOBS - 1; i > 0; i--) {
                if (jobs[i] != NULL && jobs[i]->foreground) {
                    bgJob = jobs[i];
                    break;
                }
            }
            if (bgJob == NULL) {
                printf("No jobs found\n");
                killJob(job->id);
                return EXIT_CHILD;
            }
            // move job to background
            bgJob->foreground = false;
            tcsetpgrp(fileno(stdin), getpgid(shell));
            bgJob = NULL;
        } else if (job->cmds[0][0][0] == 1) {
            Job *bgJob = jobs[atoi(job->args[1])];
            if (bgJob == NULL) {
                printf("Job does not exist\n");
                killJob(job->id);
                return EXIT_CHILD;
            } else if (!bgJob->foreground) {
                printf("Job is already in the background\n");
                killJob(job->id);
                return EXIT_CHILD;
            }
            // move job to background
            bgJob->foreground = false;
            tcsetpgrp(fileno(stdin), getpgid(shell));
            bgJob = NULL;
        }
        killJob(job->id);
    } else { // no build-in command found
        pid_t pid; // process id to differentiate from root child and parent (shell)
        pid_t pgid = 0; // process group id, defined using the pid of the first process called
        int fdin = fileno(stdin); // initialize to read from stdin
        int fd[2]; // keep track of fdin and fdout for each process

        int i;
        for (i = 0; i < npipe + 1; ++i) {
            if (i < npipe) {
                if (pipe(fd) < 0) printf("Piping failed\n");
            }
            if ((pid = fork()) == 0) {
                if (fdin != fileno(stdin) && i < npipe) {
                    dup2(fdin, fileno(stdin));
                    close(fdin);
                }

                int fdout = fd[1];
                if (fdout != fileno(stdout) && i < npipe) {
                    dup2(fdout, fileno(stdout));
                    close(fdout);
                }
                
                if (i == npipe) dup2(fdin, fileno(stdin));
                execvp(job->cmds[i][1], &(job->cmds[i][1]));
            } else if (pid < 0) {
                printf("Fork failed\n");
            } else {
                // set pgid of process group to pid of root child
                if (i == 0) {
                    pgid = pid;
                    job->pgid = pid;
                    setpgid(pid, pgid);
                } else {
                    setpgid(pid, pgid);
                }
            }
            if (i < npipe) {
                close(fd[1]);
                fdin = fd[0];
            }   
        }

        // wait for child, only if process in foreground
        if (job->foreground) {
            // give control of terminal to the foreground process
            tcsetpgrp(fileno(stdin), job->pgid);
            int stat;
            waitpid(-pgid, &stat, WUNTRACED);
        }
        // reassign control of terminal to shell
        tcsetpgrp(fileno(stdin), getpgid(shell));
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
    // create a new job that will run this line of the shell
    Job *job = malloc(sizeof(Job));

    // set up line parser variables
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
    int maxArgs = MAX_ARGS;
    job->args = calloc(MAX_ARGS, sizeof(char*));
    for (int i = 0; i < MAX_ARGS; i++) job->args[i] = calloc(BUFFER_SIZE, sizeof(char));
    int nargs = 0;
    int counter = 0; // iterates through cmd/args
    for (int i = 0; i < linelen; i++) {
        // double size of args array if no space left
        if (nargs >= maxArgs) {
            char temp[maxArgs][BUFFER_SIZE];
            for (int arg = 0; arg < nargs; arg++) {
                strcpy(temp[arg], job->args[arg]);
                free(job->args[arg]);
            }
            free(job->args);
            job->args = calloc(maxArgs *= 2, sizeof(char*));
            for (int arg = 0; arg <= maxArgs; arg++) {
                job->args[arg] = calloc(BUFFER_SIZE, sizeof(char));
                if (arg < nargs) strcpy(job->args[arg], temp[arg]);
            }
        }
        if (line[i] == ' ' || line[i] == '\n') {
            // convert to string
            job->args[nargs][counter] = '\0';

            // break on newline, since end of line reached
            if (line[i] == '\n') break;

            // go to next arg, reset cmd/arg iterator
            nargs++;
            counter = 0;
            continue;
        }
        
        job->args[nargs][counter++] = line[i];
    }
    job->args[nargs + 1] = NULL;

    // TODO: remove
    // test print to show what parsed cmd & args are
    // for (int i = 0; i < maxArgs; i++) {
    //     printf("arg %d is: %s\n", i, job->args[i]);
    // }

    if (!strcmp(job->args[nargs], "&")) job->foreground = false;
    else job->foreground = true;
    // assign the process an id and send any foreground job to background
    int assigned = false;
    for (int i = 1; i < MAX_JOBS; i++) {
        if (jobs[i] == NULL && !assigned) {
            job->id = i;
            jobs[i] = job;
            assigned = true;
        } else if (jobs[i] != NULL && jobs[i]->foreground) {
            jobs[i]->foreground = false;
        }
    }

    // parse pipe characters so that args can be piped
    int npipe = 0;
    for (int i = 0; i < nargs; i++) { if (!strcmp(job->args[i], "|")) npipe++; }
    job->cmds = calloc(npipe + 1, sizeof(char**)); // # of cmds will always be # of pipes + 1
    for (int i = 0; i < npipe + 1; i++) {
        job->cmds[i] = calloc(maxArgs, sizeof(char*));
        for (int j = 0; j < maxArgs; j++) {
            job->cmds[i][j] = calloc(BUFFER_SIZE, sizeof(char));
        }
    }

    int arg = 0; // index of arg from args
    int cmd = 0; // which cmd we're on
    int cmdArg = 0; // which arg of this cmd we're on
    while (job->args[arg] != NULL) {
        // if (args[arg + 1] == NULL) printf("next is null\n");
        if (!strcmp(job->args[arg], "&") && arg == nargs) break;
        if (!strcmp(job->args[arg], "|")) {
            if (arg == nargs) { // last arg is a pipe
                printf("Pipe has no target\n");
                free(line); 
                killJob(job->id);
                job = NULL;
                free(job);
                return -1;
            }
            job->cmds[cmd][cmdArg + 1] = NULL;
            job->cmds[cmd++][0][0] = cmdArg - 1; // store # of args for cmd in 0 index
            cmdArg = 0;
            arg++;
        }
        // reserve cmds[cmd][0] for # of args for that command
        strcpy(job->cmds[cmd][1 + cmdArg++], job->args[arg++]);
    }
    // write null terminator and number of args for last arg (won't have pipe)
    job->cmds[cmd][cmdArg + 1] = NULL;
    job->cmds[cmd++][0][0] = cmdArg - 1;

    // write other relevant information to job array
    job->npipe = npipe;
    job->maxArgs = maxArgs;

    // TODO: remove
    // test print to see if pipes are being parsed properly
    // for (int i = 0; i < npipe + 1; i++) {
    //     printf("number of args: %d\n", job->cmds[i][0][0]);
    //     for (int j = 0; j < maxArgs; j++) {
    //         printf("(%d) arg %d is: %s\n", i, j, job->cmds[i][j]);
    //     }
    // }

    // printf("The PID of the shell is: %d\n", getpid());

    // call cmd list
    int err = runargs(npipe, job);

    // free line parser
    free(line); 
    // job is pointed to by pointer in jobs array, unassign job pointer
    job = NULL;
    free(job);

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

    // struct sigaction sa;
    // sigaction(SIGCHLD, &sa, NULL);
    // sa.sa_handler = killChild;
    // signal(SIGCHLD, SIG_IGN);


    while (true) {
        int err;
        if ((err = readline(stdin)) < 0) return -1;
        else if (err == 1) return 0;
        killZombies();
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
    
    while (true) {
        int err;
        if ((err = readline(fp)) < 0) return -1;
        else if (err == 1) return 0;
        killZombies();
    }

    return 0;
}

int main(int argc, char *argv[]) {
    shell = getpid();
    printf("PID of shell: %d\n", shell);
    // set up signal handlers
    signal(SIGTTOU, SIG_IGN);
    signal(SIGINT, sigHandler);
    signal(SIGTSTP, sigHandler);
    // assign current process (what will be the shell) as root process
    setpgid(0, 0);
    tcsetpgrp(fileno(stdin), getpid());
    
    if (argc == 1) exit(interactive());
    else if (argc == 2) exit(batch(argv[1]));
}