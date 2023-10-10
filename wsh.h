#ifndef WSH_H_
#define WSH_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#define BUFFER_SIZE 256
#define MAX_JOBS 128
#define MAX_ARGS 8
#define false 0
#define true 1

typedef struct Job {
    int maxArgs;
    int npipe;
    int foreground; // boolean that stores if the process is in fg
    int id;
    pid_t pgid;
    char **args; // args passed into shell
    char ***cmds; // parsed cmds
} Job;

void killJob();
void killZombies();
void sigHandler();
int runargs(int npipe, Job *job);
int readline(FILE* stream);
int interactive(void);
int batch(char* filename);

#endif