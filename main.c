#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>

/* struct for commands */
struct commandLine {
	char items[512][255];
	int numberOfItems;
	bool inputRedirect;
	bool outputRedirect;
	char inputFile[255];
	char outputFile[255];
	bool bgSymbol;
};

// Used to handle SIGTSTP signal commands
int sigtstpSignal = 0;

/* 
* Parse through the inputted command and separate into different items
*/
struct commandLine *parseCommandLine(char *line) {
	struct commandLine *currCommand = malloc(sizeof(struct commandLine));
	// For use with strtok_r
	char* saveptr;
	// For use with handling expand variable
	char expandVar[] = "$$";
	char* searchptr;

	// For handling < and > symbols
	char inputSymbol[] = "<";
	char outputSymbol[] = ">";
	currCommand->inputRedirect = false;
	currCommand->outputRedirect = false;

	// For handling & bg symbol
	char bgSymbol[] = "&";
	currCommand->bgSymbol = false;


	char* token = strtok_r(line, " ", &saveptr);
	int x = 0;
	while (token != NULL) {
		// if token hass "$$"
		if (strstr(token, expandVar) != NULL) {
			// get PID and convert to string
			pid_t smallshPID = getpid();
			char pidStr[12];
			sprintf(pidStr, "%d", smallshPID);

			char expandedStr[255];
			strcpy(expandedStr, token);
			searchptr = strstr(expandedStr, expandVar);
			strcpy(searchptr, pidStr);
			strcpy(currCommand->items[x], expandedStr);
			x++;
		}
		else if (strcmp(token, inputSymbol) == 0) {
			token = strtok_r(NULL, " ", &saveptr);
			strcpy(currCommand->inputFile, token);
			currCommand->inputRedirect = true;
		}
		else if (strcmp(token, outputSymbol) == 0) {
			token = strtok_r(NULL, " ", &saveptr);
			strcpy(currCommand->outputFile, token);
			currCommand->outputRedirect = true;
		}
		else {
			strcpy(currCommand->items[x], token);
			x++;
		}
		
		token = strtok_r(NULL, " ", &saveptr);
	}
	// if "&" symbol was last item on command line, handle here
	int lastItemIndex = x - 1;
	if (strcmp(bgSymbol, currCommand->items[lastItemIndex]) == 0) {
		x--;
		if (sigtstpSignal == 0) {
			currCommand->bgSymbol = true;
		}
	}

	currCommand->numberOfItems = x;
	return currCommand;

}

/* Signal handler for SIGINT */
void handle_SIGINT(int signo) {
	char* message = "terminated by signal 2\n";

	write(STDOUT_FILENO, message, 24);
	fflush(stdout);

}

/* Signal handler for SIGTSTP first call */
void handle_SIGTSTP(int signo) {
	if (sigtstpSignal == 0) {
		char* message = "\nEntering foreground-only mode (& is now ignored)\n";
		write(STDOUT_FILENO, message, 51);
		fflush(stdout);
		sigtstpSignal = 1;
	}
	else {
		char* message = "\nExiting foreground-only mode\n";
		write(STDOUT_FILENO, message, 31);
		fflush(stdout);
		sigtstpSignal = 0;
	}
	
}

/*
* Process the file provided as an argument to the program to
* create a linked list of movie structsand print out the list.
* Compile the program as follows :
* gcc --std=gnu99 -o smallsh main.c
*/
int main(void) {
	// initialize sigaction structs
	struct sigaction ignore_action = { 0 };

	// The ignore_action struct as SIG_IGN as its signal handler
	ignore_action.sa_handler = SIG_IGN;

	// SIGINT and SIGTSTP will ignore at first
	sigaction(SIGINT, &ignore_action, NULL);
	sigaction(SIGTSTP, &ignore_action, NULL);

	// variable to store user input
	char* userInput = NULL;
	size_t len = 0;
	ssize_t nread;

	pid_t bgPIDs[100] = { 0 };
	int bgCount = 0;

	// Used to handle comment and blank commands
	char hashtag[] = "#";
	char blank[] = " ";
	char newLine[] = "\n";
	// Used to exit out of program
	char exitStr[] = "exit";
	int exitSwitch = 0;
	// Used for status command
	char statusStr[] = "status";
	int statusCode = 0;
	// Used for cd
	char cdStr[] = "cd";

	// Used to handle "&" symbol
	char bgStr[] = "&";

	do {
		top:
		// check to see if any changes to background processes
		if (bgCount > 0) {
			pid_t checkChildPID;
			int bgStatus;
			int y;
			for (y = 0; y < bgCount; y++) {
				checkChildPID = waitpid(bgPIDs[y], &bgStatus, WNOHANG);
				if (checkChildPID > 0) {
					if (WIFEXITED(bgStatus)) {
						statusCode = WEXITSTATUS(bgStatus);
					}
					else {
						statusCode = WTERMSIG(bgStatus);
					}
					printf("Background pid %d is done: exit status %d\n", checkChildPID, statusCode);
					fflush(stdout);
				}
			}
		}
		printf(":");
		fflush(stdout);

		nread = getline(&userInput, &len, stdin);
		
		// If blank command, send back to top
		if (strcmp(blank, userInput) == 0 || strcmp(newLine, userInput) == 0) {
			goto top;
		} 


		userInput[strlen(userInput) - 1] = '\0';

		struct commandLine* currCommands = parseCommandLine(userInput);

		// If comment, then ignore and go back to top prompt
		if (strstr(currCommands->items[0], hashtag) != NULL) {
			goto top;
		}
		// if command is exit, exit out of program
		if (strcmp(currCommands->items[0], exitStr) == 0) {
			// kill all possible remaining child processes
			int z;
			for (z = 0; z < bgCount; z++) {
				kill(bgPIDs[z], SIGTERM);
			}
			exitSwitch++;
		}
		else if (strcmp(currCommands->items[0], statusStr) == 0) {
			// if status command, display status code
			printf("exit status %d\n", statusCode);
			fflush(stdout);
		}
		else if (strcmp(currCommands->items[0], cdStr) == 0) {
			// if cd command, handle here
			if (currCommands->numberOfItems == 1) {
				char* homeDir = getenv("HOME");
				chdir(homeDir);				
			}
			else {
				chdir(currCommands->items[1]);
			}
		}
		else {
			// Create array with final NULL pointer to be used by exec functions
			int size = currCommands->numberOfItems + 1;
			char* newargv[size];
			int b;
			for (b = 0; b < size; b++) {
				if (b < size - 1) {
					newargv[b] = currCommands->items[b];
				}
				else {
					newargv[b] = NULL;
				}
			}

		
			pid_t spawnPid = fork();
			int spawnStatus;
			
			switch (spawnPid) {
				case -1:
					perror("fork()\n");
					exit(1);
					break;
				case 0:
					// In the child process

					if (currCommands->bgSymbol == false) {
						struct sigaction SIGINT_fg_action = { 0 };
						// Fill out the SIGINT_fg_action struct
						SIGINT_fg_action.sa_handler = SIG_DFL;
						sigaction(SIGINT, &SIGINT_fg_action, NULL);
					}

					if (currCommands->inputRedirect == true) {
						// If there is an input redirect command
						int sourceFD = open(currCommands->inputFile, O_RDONLY);
						if (sourceFD == -1) {
							perror("source open() error ");
							exit(1);
						}

						// Redirect stdin to source file
						int result = dup2(sourceFD, 0);
						if (result == -1) {
							perror("source dup2() error ");
							exit(1);
						}

					}
					else if (currCommands->bgSymbol == true && currCommands->inputRedirect == false) {
						// redirect background input to /dev/null
						int sourceFD = open("/dev/null", O_RDONLY);
						if (sourceFD == -1) {
							perror("source open() error ");
							exit(1);
						}
						// Redirect stdin to /dev/null
						int result = dup2(sourceFD, 0);
						if (result == -1) {
							perror("BG source dup2() error ");
							exit(1);
						}
					}


					if (currCommands->outputRedirect == true) {
						// If there is an output redirect command
						int targetFD = open(currCommands->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0664);
						if (targetFD == -1) {
							perror("target open() error ");
							exit(1);
						}

						// Redirect stdout to target file
						int result = dup2(targetFD, 1);
						if (result == -1) {
							perror("target dup2() error ");
							exit(1);
						}
					}
					else if (currCommands->bgSymbol == true && currCommands->outputRedirect == false) {
						// redirect background output to /dev/null
						int targetFD = open("/dev/null", O_WRONLY, 0664);
						if (targetFD == -1) {
							perror("target open() error ");
							exit(1);
						}

						// Redirect stdout to /dev/null
						int result = dup2(targetFD, 1);
						if (result == -1) {
							perror("BG target dup2() error ");
							exit(1);
						}
					}


					execvp(newargv[0], newargv);
					perror("execvp error: ");
					exit(1);
					break;
				default:
					// In the parent process
					;

					struct sigaction SIGTSTP_action = { 0 };
					// Fill out the SIGINT_action struct
					SIGTSTP_action.sa_handler = handle_SIGTSTP;
					// Block all catchable signals while handle_SIGINT is running
					sigfillset(&SIGTSTP_action.sa_mask);
					// No flags set
					SIGTSTP_action.sa_flags = 0;
					sigaction(SIGTSTP, &SIGTSTP_action, NULL);
					
					if (currCommands->bgSymbol == false) {
						
						struct sigaction SIGINT_action = { 0 };
						// Fill out the SIGINT_action struct
						SIGINT_action.sa_handler = handle_SIGINT;
						// Block all catchable signals while handle_SIGINT is running
						sigfillset(&SIGINT_action.sa_mask);
						// No flags set
						SIGINT_action.sa_flags = 0;
						sigaction(SIGINT, &SIGINT_action, NULL);

						spawnPid = waitpid(spawnPid, &spawnStatus, 0);

						if (WIFEXITED(spawnStatus)) {
							statusCode = WEXITSTATUS(spawnStatus);
						}
						else {
							statusCode = WTERMSIG(spawnStatus);
						}
					}
					else {
						bgPIDs[bgCount] = spawnPid;
						printf("Background pid is %d\n", spawnPid);
						fflush(stdout);
						bgCount++;
					}
					break;
			}
		}

	} while (exitSwitch == 0);

	return EXIT_SUCCESS;
}