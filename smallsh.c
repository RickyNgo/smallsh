#define _POSIX_SOURCE
#define SA_RESTART 0x0002
#include <sys/types.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/wait.h>

#define MAX_COMMAND_LENGTH 2048
#define MAX_ARGUMENTS 512
#define MAX_PIDS 64

/*Will hold an array of child process PIDs so that when the user wants to exit,
 all children will be killed before the parent is*/ 
pid_t childPID[MAX_PIDS] = {0};
int numChildren = 0;
int childExitMethod = INT_MIN;

/*Flag for foreground-only*/
int foregroundOnly = 0;

/*Flags for redirections*/
int target = 0;
int source = 0;


/*Prints either the exit status or exit signal of the last foreground process*/
void getStatus()
{
	/*Returns a non-zero number if child exited normally*/	
	if (WIFEXITED(childExitMethod) != 0)
	{	
		int exitStatus = WEXITSTATUS(childExitMethod);
		printf("%d\n", exitStatus);
		return;
	}
	/*Returns a non-zero number if child exited from a signal*/
	if (WIFSIGNALED(childExitMethod) != 0)
	{
		int termSignal = WTERMSIG(childExitMethod);
		printf("%d\n", termSignal);
		return;
	}	
}

/*Signal handler for ^C for child processes*/
void catchChildSIGINT(int signal)
{
	char *message = "Caught a SIGINT signal\n";
	sprintf(message, "%d\n", signal);
	write(STDOUT_FILENO, message, 24);
	raise(SIGTERM);
	fflush(NULL);
	sleep(5);
}

/*Signal handler for ^C for parent process*/
void catchParentSIGINT(int signal)
{
	char *message = "Caught a SIGINT signal\n";
	write(STDOUT_FILENO, message, 24);
	fflush(NULL);
	sleep(5);
}

/*Signal handler for ^Z. Toggles the flag for foreground-only mode*/
void catchSIGTSTP(int signal)
{
	char *message = "Entering foreground-only mode (& is now ignored)\n";
	char *message2 = "Exiting foreground-only mode\n";

	if (foregroundOnly)
	{
		write(STDOUT_FILENO, message2, 30);
		foregroundOnly = 0;
	}
	else
	{
		write(STDOUT_FILENO, message, 50);
		foregroundOnly = 1;
	}
	
	fflush(NULL);
	sleep(5);	
}

/*Will take a users input from the keyboard and parse it to check for any arguments/options
 and execute the correct function*/

char** stringArgs(char *input)
{
	/*Declare an array of strings to hold each token from the original input*/
	char **arguments;
	
	arguments = malloc(sizeof(char*));
	
	/*Get the first token*/	
	char *token = strtok(input, " &\n");
	
	/*Initialize the first array index*/	
	int argCount = 0;
	arguments[argCount] = malloc(256*sizeof(char));	
	
	arguments[argCount] = token;
	
	/*While not at the end of the input string, keep tokenizing and placing them into the array*/
	while (token != NULL)
	{
		token = strtok(NULL, " &\n");
		
		argCount++;
		arguments[argCount] = malloc(256*sizeof(char));
		arguments[argCount] = token;
	}
/*	
	for (int i = 0; i < argCount; i++)
	{
		printf("Args[%d]: %s\n", i, arguments[i]);
	}*/
	/*Looks for instances of $$ to expand into the PID*/
/*	for (int i = 0; i < argCount; i++)
	{		
		if (strstr(arguments[i], "$$") != NULL)
		{
			arguments[i] = strtok(arguments[i], " $\n");
			
			if (arguments[i] != NULL)
			{		
				sprintf(arguments[i], "%s%d", arguments[i], getpid());
			}
			else
			{
				char temp[10];
				sprintf(temp, "%d", getpid());
				
				arguments[i] = temp;
			}	
			break;
		}
	}
*/

	
	char *tword = NULL;
	char modWord[256];
	memset(modWord, '\0', sizeof(modWord));

	for (int i = 0; arguments[i] != NULL; i++)
	{
		if((tword=strstr(arguments[i], "$$")) != NULL)
		{
			char *front = NULL;
			front = strtok(arguments[i], "$");
			
			if (front == NULL)
			{
				sprintf(modWord, "%d%s", getpid(), tword+2);
			}
			else
			{
				sprintf(modWord, "%s%d%s", front, getpid(), tword+2);
			}

			arguments[i] = modWord;
		}
	}
	

	arguments[argCount] = NULL;
	
/*	for (int i = 0; i < argCount; i++)
	{
		printf("Args[%d]: %s\n", i, arguments[i]);
	}*/
	return arguments;	
}

/*Returns the number of non-NULL arguments*/
int getArgCount(char **args)
{
	int numArgs = 0;
	
	for (int i = 0; args[i] != NULL; i++)
	{
		numArgs++;
	}

	return numArgs;
}

/*Cleanup function to reap any child processes*/
void exitProgram()
{
	printf("Exiting program\n");
	if (childPID != NULL)
	{	
		
		for (int i = 0; i < numChildren; i++)
		{
			int result = kill(childPID[i], SIGKILL);
			waitpid(childPID[i], &childExitMethod ,0);
		}
	}
	fflush(NULL);
	exit(0);	
}

/*Takes relative and absolute paths to change directory too. If no path is specified,
 it will change directory the the HOME environment*/
void cd(char *input)
{	
	/*Generate argument tokens*/
	char **args = stringArgs(input);

	/*Get the current directory*/
	char currentDir[256];
	getcwd(currentDir, 256);
	
	/*If no path specified, then cd to HOME*/
	if (args[1] == NULL)
	{
		chdir(getenv("HOME"));
	}
	/*Checks whether or not the user wants a relative path*/
	else if (args[1][0] != '/')
	{
		/*Tack on the "/" and inputed path, then cd into it*/
		strcat(currentDir, "/");
		strcat(currentDir, args[1]);
		int result = chdir(currentDir);
		
		if (result != 0)
		{
			fprintf(stderr, "%s", "No such file or directory.\n");
		}
	}
	/*Otherwise, cd into the inputed absolute path*/
	else
	{
		int result = chdir(args[1]);
		
		if (result != 0)
		{
			fprintf(stderr, "%s", "No such file or directory.\n");
		}
	}
}

/*Wraps the fork() syscall so that the list of child PID's can be tracked in an array*/
pid_t newProcess()
{
	pid_t spawnPID = fork();
	childPID[numChildren] = spawnPID;
	numChildren++;

	return spawnPID;
}

/*Checks for redirection arguments and sets the correct file descriptors*/
/*If runInBackground was set, then the command will not be waited on*/
void runProcess(int runInBackground, char **arguments, int argCount)
{
	/*Ampersand was entered as the last argument. Run the process in the background*/
		
	char *targetFile = malloc(64*sizeof(char*));
	char *sourceFile = malloc(64*sizeof(char*));

	char **test = malloc(sizeof(char*));
	
		
	/*This loop does nothing, but it makes certain conditions work*/
	for (int i = 0; arguments[i] != NULL;i++)
	{
		test[i] = malloc(256*sizeof(char));
		strcpy(test[i], arguments[i]);
		test[i+1] = NULL;
		//printf("IN PROCESS: %s\n", arguments[i]);
	}
	
	/*Checks and sets the file descriptors if < or > are found*/	
	for (int i = 0; i < argCount-1; i++)
	{
		if (arguments[i] != NULL)
		{
		if (strcmp(arguments[i], ">") == 0)
		{
			target = 1;
			strcpy(targetFile, arguments[i+1]);
		}

		if (strcmp(arguments[i], "<") == 0)	
		{
			source = 1;
			strcpy(sourceFile, arguments[i+1]);
		}}
	}

	/*If foregroundOnly is set, then run the program in foreground regardless if an & was passed*/
	if (runInBackground == 1 && foregroundOnly != 1)
	{	
		/*Fork() a new process*/
		pid_t spawnPID = newProcess();
		if (spawnPID == -1)
		{
				perror("Hull Breach!");
				exit(1);
		}
		/*If fork was successful*/
		else if (spawnPID == 0)
		{		
				/*Set the background file descriptors to dev/null*/
				int devNull1 = open("/dev/null", O_WRONLY);
				int devNull2 = open("/dev/null", O_RDONLY);
				
				dup2(devNull1, 1);
				dup2(devNull2, 0);
	
				close(devNull1);
				close(devNull2);
				
				/*Run the commands*/
				int result =  execvp(test[0], test);
				if (result == -1)
				{
					fprintf(stderr, "Command ran into an error.\n");
					exit(1);
				}
				exit(0);
		}
		else
		{
				sleep(1);
				printf("Background PID is %d\n", spawnPID);
		}
	}
	/*No ampersand, run in the foreground and have the parent wait until the child finishes*/
	else
	{
		int exitStatus = -5;
		pid_t spawnPID = fork();
		switch(spawnPID)
		{
			case -1:
				perror("Hull Breach!");
				exit(1);
			case 0:
				/*Set up signal() specifically for the child*/
				signal(SIGINT, catchChildSIGINT);
				int targetFD = 0;
				int sourceFD = 0;
				
				/*If the redirection flags were set, then attempt to open the target/source
 				 and point the file descriptors in the correct places*/
				if (target == 1)
				{
					targetFD = open(targetFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
					if (targetFD < 0)
					{
						fprintf(stderr, "Couldn't open the file\n");
						exit(1);
					}
					else 
					{
						dup2(targetFD, 1);
					}
				}
					
				if (source == 1)
				{
					sourceFD = open(sourceFile, O_RDONLY);
					if (sourceFD < 0)
					{
						fprintf(stderr, "Couldn't open the file\n");
						exit(1);
					}
					else 
					{
						dup2(sourceFD, 0);
					}
				}
				
				/*If redirections were required, use execlp() instead of execvp() so that
 				 the original arguments array doesn't need to be modified*/
				int result;
				if (target == 1 || source == 1)
				{
					result = execlp(arguments[0], arguments[0], NULL);	
				}
				else
				{
				 	//result = execvp(arguments[0], arguments);
				 	result = execvp(test[0], test);
			
				}
			

				if (result == -1)
				{
					fprintf(stderr, "Command ran into an error.\n");
					exit(1);
				}
				else
				{
					exit(0);
				}
			default:
				/*Clean up any foreground processes before returning the parent*/	
				while(waitpid(spawnPID, &exitStatus, 0) == -1);
				
				/*If the child exited due to a signal, print it out*/
				if (WIFSIGNALED(exitStatus) != 0)
				{
					int termSignal = WTERMSIG(exitStatus);
					printf("Signal: %d\n", termSignal);
				}
		}
	}
	
	fflush(NULL);
}

/*Initializes the args array and argCount*/
void bashCommands(char *input, int runInBackground)
{	char **args = stringArgs(input);
	int argCount = getArgCount(args);
	runProcess(runInBackground, args, argCount);
}

/*Initial screen of the user's input for basic functions such as cd, status, exit*/
void parseCommand(char *input)
{
	
	int runInBackground = 0;
	
	/*Check for &*/
	for (int i = 0; i < strlen(input); i++)
	{
		if (input[i] == '&')
		{
			runInBackground = 1;
			break;
		}
	}
	
	/*Check for exit*/
	if (strcmp(input, "exit\n") == 0)
	{
		exitProgram();
	}
	/*Check for cd*/
	else if (input[0] == 'c' && input[1] == 'd')
	{
		cd(input);
	}
	/*Check for status*/
	else if (input[0] == 's'
		&& input[1] == 't'
		&& input[2] == 'a'
		&& input[3] == 't'
		&& input[4] == 'u'
		&& input[5] == 's')
	{	
		if (runInBackground)
		{
			pid_t spawnPID = newProcess();
			switch(spawnPID)
			{
				case -1:
					perror("Hull Breach!");
					exit(1);
					break;
				case 0:
					getStatus();
					exit(0);
					break;
				default:
					sleep(1);
					printf("Background PID is %d\n", spawnPID);
					break;
			}
			return;
		}
		else
		{
			getStatus();
		}

	}
	/*Empty line*/
	else if (strcmp(input, "\n") == 0 )
	{
		return;
	}
	/*Comment*/
	else if (input[0] == '#')
	{
		return;
	}
	/*Try to run commands via execvp/lp*/
	else
	{
		bashCommands(input, runInBackground);
	}


}

/*Driver function*/
void main (int argc, char argv[])
{
	int numCharsEntered = INT_MIN;
	char buffer[MAX_COMMAND_LENGTH];

	/*Setup the signal handlers*/	
	struct sigaction SIGTSTP_action = {0};
	SIGTSTP_action.sa_handler = catchSIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = SA_RESTART;

	struct sigaction SIGINT_action = {0};
	SIGINT_action.sa_handler = catchParentSIGINT;
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;	
	
	/*Launch the signal handlers*/
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
	sigaction(SIGINT, &SIGINT_action, NULL);	

	while(1)
	{
		printf(": ");
		
		/*Get user input*/
		char *result = fgets(buffer, MAX_COMMAND_LENGTH, stdin);
		
		/*Run it through*/
		parseCommand(buffer);
		
		/*Try to cleanup after each execvp() call*/
		/*Iterates through the list of child processes to see if any have terminated*/	
		pid_t pid;
		for (int i = 0; i < numChildren; i++)
		{
			if (childPID[i] != 0)
			{
				int status;
				pid = waitpid(childPID[i], &status, WNOHANG);
				if (pid == 0)
				{
				}
				else if (pid == -1)
				{
				}
				else
				{
					
					printf("PID: %d terminated with status ", childPID[i]);
					getStatus();
				}
			}	
		}
		/*Reset redirection flags*/
		source = 0;
		target = 0;
		memset(buffer, '\0', MAX_COMMAND_LENGTH);
	}
}

