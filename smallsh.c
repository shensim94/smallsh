/*
Name: smallsh, a limited-feature shell written in c
Synopsis: command [arg1 arg2 ...] [< input_file] [> output_file] [&]
Arthur: Simon Shen
Date[last modified]: 5/9/2022
*/
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>

int exitStatus = 0; // made this global since this gets used just about everywhere
int allowBg = 1;

typedef struct{
    char* arg[512];
    char* inputfile;
    char* outputfile;
} cmdInput;

char *
__strdup (const char *s)       // source code for strdup, provided by professor in ed#85
{
  size_t len = strlen (s) + 1;
  void *new = malloc (len);
  if (new == NULL)
    return NULL;
  return (char *) memcpy (new, s, len);
}


/* takes input from stdin and break them down into arguments to be processed */
int parseInput(cmdInput* input, int pid, int* bg){
    
    /*get the command line input*/
    char inputString[2048];
    printf(": "); // command prompt
    fflush(stdout);
    fgets(inputString, 2048, stdin);

    // skip comments and non inputs
    if(inputString[0]=='#' || inputString[0] == '\n'){
        return 1;
    }

    // skip blank lines
    int x = 0;
    while(inputString[x] == ' '){
        if(inputString[x+1] == '\n'){
            return 1;
        }
        x++;
    }

    // convert pid to string
    char *pidstr;
    int n = snprintf(NULL, 0, "%d", pid);
    pidstr = malloc((n + 1) * sizeof *pidstr);
    sprintf(pidstr, "%d", pid);


    /* strips out newline character at the end */
    if(inputString[strlen(inputString)-1] == '\n'){
        inputString[strlen(inputString)-1] = '\0';
    };

    /* count the number of instances "$$" appears */
    int count = 0;
    const char *tmp = inputString;
    while((tmp = strstr(tmp, "$$"))){
        count++;
        tmp = tmp+2;
    }

    /* create an expanded string that copies over the input string
    char by char and expands any instance of $$ */
    char exStr[strlen(inputString)+count*(strlen(pidstr)-2)+1];
    int j = 0;
    int k = 0;
    int in_len = strlen(inputString);
    while(j < in_len){

        if ((inputString[j] == '$') && (inputString[j+1] == '$')){
            for(size_t a=0; a < strlen(pidstr); a++){
                exStr[k] = pidstr[a];
                k++;
            }
        j=j+2;
        }

        else{
            exStr[k]=inputString[j];
            j++;
            k++;
        }
    }
    exStr[k] = '\0';

    /* parse the input into the input struct */
    int i = 0;
    char* token;
    token = strtok(exStr, " ");

    /* get all the args */
    while (token != NULL && strcmp(token, "<") && strcmp(token, ">") && strcmp(token, "&") ){

        input->arg[i] = __strdup(token); 
        i++;
        token = strtok(NULL, " ");
    }
    /* get input, output, and background boolean */
    while (token != NULL){

        if (!strcmp(token, "<")){
            token = strtok(NULL, " ");
            input->inputfile = __strdup(token);
        }

        else if (!strcmp(token, ">")){
            token = strtok(NULL, " ");
            input->outputfile = __strdup(token);
        }

        else if (!strcmp(token, "&")){
            *bg = 1;
        }
        token = strtok(NULL, " ");
    }
  return 0;
}

/* handles the following commands: exit, cd and status */
int execBuiltIn(cmdInput input){

    if(!strcmp(input.arg[0], "exit")){
        exit(0);
    }
    
    else if (!strcmp(input.arg[0], "cd")){
        (input.arg[1]) ? chdir(input.arg[1]) : chdir(getenv("HOME"));
        return 1;
    }
    
    else if (!strcmp(input.arg[0], "status")){
        if (WIFEXITED(exitStatus)){
			printf("exit value %d\n", WEXITSTATUS(exitStatus));
			fflush(stdout);
        }
        else{ // if(WIFSIGNALED(wstatus))
            printf("terminated by signal %d\n", WTERMSIG(exitStatus));
            fflush(stdout);
        }
        return 1;
    }
  
  return 0;
}


/* handle any non built in commands
   borrows heavily from Exploration: Processes and I/O */
void run(cmdInput input, int bg, struct sigaction SIGTSTP_action, struct sigaction SIGINT_action){
    int sourceFD;
    int targetFD;

	  // Fork a new process

    pid_t spawnPid = -5;
	spawnPid = fork();

	switch(spawnPid){

        case -1:
		        perror("fork()\n");
		        exit(1);
		        break;

        case 0:
            /* simply ignore SIGTSTP in the child process */
            if(bg==1){SIGINT_action.sa_handler = SIG_IGN;}
            else{SIGINT_action.sa_handler = SIG_DFL;}
            sigaction(SIGINT, &SIGINT_action, NULL);

            /* I/O redirection should be done in the child process */
            if (input.inputfile != NULL){
            // setup for input redirection
                sourceFD = open(input.inputfile, O_RDONLY);
                if (sourceFD == -1) {
		                perror("open()");
		                exit(1);
	            }
                dup2(sourceFD, 0);
                fcntl(sourceFD, F_SETFD, FD_CLOEXEC);
            }
            /*sourceFD = open("/dev/null", O_RDONLY);
            if (sourceFD == -1) {
                perror("open()");
                exit(1);
            }
            dup2(sourceFD, 0);
            fcntl(sourceFD, F_SETFD, FD_CLOEXEC);*/

            if (input.outputfile != NULL){
                // setup for output redirection
                targetFD = open(input.outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (targetFD == -1) {
                    perror("open()");
                    exit(1);
                }
                dup2(targetFD, 1);
                fcntl(targetFD, F_SETFD, FD_CLOEXEC);
            }

            execvp(input.arg[0], input.arg);
            // exec only returns if there is an error
            perror("execve");
            fflush(stdout);
            exit(2);
            break;
	
        /* parent process */
        default:
            if(bg == 0 || allowBg == 0 ){ // run in the foreground

                spawnPid = waitpid(spawnPid, &exitStatus, 0);

            }
            else if (bg == 1 && allowBg == 1){
                waitpid(spawnPid, &exitStatus, WNOHANG);
                printf("background pid is %d\n", spawnPid);
                fflush(stdout);
            }
	}
}

/* handles SIGTSTP, basically just toggles the 
  condition that allows for background processes */
void handle_SIGTSTP(int signo){
    if (allowBg == 1) {
        allowBg = 0;                  // toggle 
        char* message = "Entering foreground-only mode (& is now ignored)\n";
        // We are using write rather than printf
        write(STDOUT_FILENO, message, 49);
        fflush(stdout);
    }
    else if (allowBg == 0) {
        allowBg = 1;                  // toggle 
        char* message = "Exiting foreground-only mode\n";
        // We are using write rather than printf
        write(STDOUT_FILENO, message, 29);
        fflush(stdout);
    }
}

void handle_zombies(int signo){
    pid_t bgPid;
    while ((bgPid = waitpid(-1, &exitStatus, WNOHANG)) > 0) {
        if (WIFEXITED(exitStatus)){
            printf("background pid %d is done: exit value %d\n: ", bgPid, WEXITSTATUS(exitStatus));
            fflush(stdout);
        }
        else{ // if(WIFSIGNALED(wstatus))
            printf("background pid %d is done: terminated by signal %d\n: ", bgPid, WTERMSIG(exitStatus));
            fflush(stdout);
        }
    }
}

int main(){

    int bg = 0;
    int pid = getpid();
    cmdInput input;

    struct sigaction SIGINT_action = {0};

	/*set up the sigaction struct per: Exploration: Signal Handling API*/
  	// Register handle_SIGINT as the signal handler
    SIGINT_action.sa_handler = SIG_IGN;
  	// Block all catchable signals while handle_SIGINT is running
    sigfillset(&SIGINT_action.sa_mask);
  	// No flags set
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);

    /*set up the sigaction struct per: Exploration: Signal Handling API*/
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    /* main execution loop runs indefinitely until cmd exit*/
    while(1){

        /* clean up the input before every loop */
        input.inputfile = NULL;
        input.outputfile = NULL;
        for (int i=0; i<512; i++) {
            input.arg[i] = NULL;
        }

        /* get inputs */
        if(parseInput(&input, pid, &bg) == 0){  // parseInput returns 0 if inputs are valid syntax(they can still be bad files)
            if(execBuiltIn(input) == 0){   // execBuiltIn returns 0 if no built-in cmds are performed
                run(input, bg, SIGTSTP_action, SIGINT_action);
            }
        }
        bg = 0;        // reset the background toggle

        /* reap dead children */
        signal(SIGCHLD, handle_zombies);
		}
      
}