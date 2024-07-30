#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_LENGTH 1024
#define MAX_ARGS 64
#define PROMPT "mysh> "



void executeCommand(char *tokens[], int numTokens);
int tokenizeCommand(char *command, char *tokens[]);
char *searchProgram(char *program);
void executeCd(char *tokens[], int numTokens);
void executePwd(char *tokens[], int numTokens);
void executeWhich(char *tokens[], int numTokens);
void expandWildcards(char *tokens[], int numTokens);


void executeCommand(char *tokens[], int numTokens) {
    if (strcmp(tokens[0], "cd") == 0) {
        executeCd(tokens, numTokens);
    } else if (strcmp(tokens[0], "pwd") == 0) {
        executePwd(tokens, numTokens);
    } else if (strcmp(tokens[0], "which") == 0) {
        executeWhich(tokens, numTokens);
    } else {

        char *inputFile = NULL;
        char *outputFile = NULL;

        char *cmd1[MAX_ARGS + 2];
        char *cmd2[MAX_ARGS + 2];

        int pipefd[2]; 
        bool pipeOn = false; 

        for (int i = 0; i < numTokens; ++i) {
            if (strcmp(tokens[i], "<") == 0) {
                inputFile = tokens[i + 1];
                memmove(&tokens[i], &tokens[i + 2], (numTokens - i - 1) * sizeof(char *));
                numTokens -= 2;
            } else if (strcmp(tokens[i], ">") == 0) {
                outputFile = tokens[i + 1];
                memmove(&tokens[i], &tokens[i + 2], (numTokens - i - 1) * sizeof(char *));
                numTokens -= 2;
            } else if (strcmp(tokens[i], "|") == 0) {
                pipeOn = true;
                
                for (int j = 0; j < i; ++j) {
                    cmd1[j] = tokens[j];
                }
                cmd1[i] = NULL;
                for (int j = 0; j < numTokens - i - 1; ++j) {
                    cmd2[j] = tokens[i + j + 1];
                }
                cmd2[numTokens - i - 1] = NULL; 

                if (pipe(pipefd) == -1) {
                    perror("Pipe creation failed");
                    exit(EXIT_FAILURE);
                }

                break;
            }
        }

        expandWildcards(tokens, numTokens);

        pid_t pid = fork();
        if (pid == -1) {
            perror("Fork failed");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {

            if (inputFile != NULL) {
                freopen(inputFile, "r", stdin);
            }

            if (pipeOn) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
            } else if (outputFile != NULL) {
                freopen(outputFile, "w", stdout);
            }

            if (pipeOn) {
                execvp(cmd1[0], cmd1);
            } else {
                execvp(tokens[0], tokens);
            }
            perror("Execution failed");

            char *programPath = searchProgram(tokens[0]);
            if (programPath != NULL) {
                execv(programPath, tokens);
                perror("Execution failed");
            }

            fprintf(stderr, "Command not found: %s\n", tokens[0]);
            exit(EXIT_FAILURE);
        } else {
            int current;
            waitpid(pid, &current, 0);

            if (pipeOn) {
                close(pipefd[1]);

                pid_t pid2 = fork();
                if (pid2 == -1) {
                    perror("Second fork failed");
                    exit(EXIT_FAILURE);
                } else if (pid2 == 0) {
                    dup2(pipefd[0], STDIN_FILENO);
                    close(pipefd[0]);

                    execvp(cmd2[0], cmd2);
                    perror("Execution failed");

                    char *programPath = searchProgram(cmd2[0]);
                    if (programPath != NULL) {
                        execv(programPath, cmd2);
                        perror("Execution failed");
                    }

                    fprintf(stderr, "Command not found: %s\n", cmd2[0]);
                    exit(EXIT_FAILURE);
                } else {
                    waitpid(pid2, &current, 0);
                }
            }

            if (strcmp(tokens[0], "exit") == 0) {
                printf("mysh: exiting\n");
                exit(EXIT_SUCCESS);
            }
        }
    }
}


int tokenizeCommand(char *command, char *tokens[]) {
    int numTokens = 0;
    char *token = strtok(command, " \t\n");

    while (token != NULL && numTokens < MAX_ARGS + 2) {
        tokens[numTokens++] = token;
        token = strtok(NULL, " \t\n");
    }

    tokens[numTokens] = NULL;  
    return numTokens;
}

char *searchProgram(char *program) {
    char *directories[] = {"/usr/local/bin", "/usr/bin", "/bin"};
    int numDirectories = sizeof(directories) / sizeof(directories[0]);

    static char programPath[PATH_MAX];

    if (strchr(program, '/') != NULL) {
        if (access(program, X_OK) == 0) {
            return program;
        }
    } else {
        for (int i = 0; i < numDirectories; ++i) {
            snprintf(programPath, sizeof(programPath), "%s/%s", directories[i], program);
            if (access(programPath, X_OK) == 0) {
                return programPath;
            }
        }
    }

    return NULL; 
}

void expandWildcards(char *tokens[], int numTokens) {
    for (int i = 0; i < numTokens; ++i) {
        if (strchr(tokens[i], '*') != NULL) {
            glob_t gNum;
            memset(&gNum, 0, sizeof(gNum));

            if (glob(tokens[i], GLOB_NOCHECK, NULL, &gNum) == 0) {
                for (size_t j = 0; j < gNum.gl_pathc; ++j) {
                    tokens[i + j] = strdup(gNum.gl_pathv[j]);
                }

                memmove(&tokens[i + gNum.gl_pathc],
                        &tokens[i + 1],
                        (numTokens - i - 1) * sizeof(char *));
                numTokens += gNum.gl_pathc - 1;

                globfree(&gNum);
            }
        }
    }
}

void executeCd(char *tokens[], int numTokens) {
    if (numTokens != 2) {
        fprintf(stderr, "cd: wrong num of arguments\n");
        return;
    }

    if (chdir(tokens[1]) != 0) {
        perror("cd failed");
    }
}

void executePwd(char *tokens[], int numTokens) {
    if (numTokens != 1) {
        fprintf(stderr, "pwd: wrong num of arguments\n");
        return;
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("getcwd() error");
    }
}

void executeWhich(char *tokens[], int numTokens) {
    if (numTokens != 2) {
        fprintf(stderr, "which: wrong num of arguments\n");
        return;
    }

    char *programPath = searchProgram(tokens[1]);
    if (programPath != NULL) {
        printf("%s\n", programPath);
    } else {
        fprintf(stderr, "which: command not found: %s\n", tokens[1]);
    }
}


void interactiveMode() {
    printf("Welcome to my shell!\n");
    while (1) {
        printf("mysh> ");

        char line[MAX_LENGTH];
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        size_t length = strlen(line);
        if (length > 0 && line[length - 1] == '\n') {
            line[length - 1] = '\0';
        }

        char *tokens[MAX_ARGS + 2];  
        int numTokens = tokenizeCommand(line, tokens);

        executeCommand(tokens, numTokens);

        if (strcmp(line, "exit") == 0) {
            printf("mysh: exiting\n");
            break;
        }
    }
}

void batchMode(FILE *file) {
    char line[MAX_LENGTH];

    while (fgets(line, sizeof(line), file) != NULL) {
        size_t length = strlen(line);
        if (length > 0 && line[length - 1] == '\n') {
            line[length - 1] = '\0';
        }

        char *tokens[MAX_ARGS + 2];  
        int numTokens = tokenizeCommand(line, tokens);

        executeCommand(tokens, numTokens);

        if (strcmp(line, "exit") == 0) {
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc == 2) {
        FILE *file = fopen(argv[1], "r");
        if (file == NULL) {
            perror("Error opening file");
            return EXIT_FAILURE;
        }

        batchMode(file);
        fclose(file);
    } else if (argc == 1) {
        interactiveMode();
    } else {
        fprintf(stderr, "Usage: %s [file]\n", argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
