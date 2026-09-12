#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Todo: fork(); in week lub baby
void startShell() {
    char input[1024];
    while(1){
        printf("shellix> ");
        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = 0;
        if (strcmp(input, "exit") == 0) {
            break;
        }
        printf("Command: %s\n", input);
    }
}