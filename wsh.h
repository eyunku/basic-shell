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
#define MAX_ARGS 8

int interactive(void);
int batch(char* filename);

#endif