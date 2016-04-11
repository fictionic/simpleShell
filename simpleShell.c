#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/wait.h>

// A line may be at most 100 characters long, which means longest word is 100 chars, 
// and max possible tokens is 51 as must be space between each
size_t MAX_TOKEN_LENGTH = 100;
size_t MAX_NUM_TOKENS = 51;

char* readLine();
char** lex(char* line);
char*** parse(char* line);

int main() {
	while(1) {
		fflush(stdout);
		printf("simpleShell $ ");

		// read input into string
		char* line = readLine();
		if(line == NULL)
			return 1;

		// lex string into array of tokens
		char** tokens = lex(line);

		// check if we have any tokens
		if(tokens[0] == NULL)
			continue;

		// PARSING //
		// -- iterate through tokens -- //
		// first, determine number of commands
		int i = 0;
		int num_commands = 1;
		while(tokens[i] != NULL) {
			if(tokens[i][0] == '|' && tokens[i][1] == '\0') {
				num_commands++;
			}
			i++;
		}
		
		// make sure the first token isn't a pipe
		if(tokens[0][0] == '|' && tokens[0][1] == '\0') {
			printf("Error: can't have pipe be first token\n");
			return 1;
		}

		// check if the last token is '&'
		int no_wait;
		if(tokens[i-1][0] == '&' && tokens[i-1][1] == '\0') {
			no_wait = 1;
			// replace the last token with a null pointer
			free(tokens[i-1]);
			tokens[i-1] = NULL;
		} else
			no_wait = 0;

		// assemble tokens into array of commands
		// first, allocate arrays for commands, their lengths, and the pipes that will connect them
		char*** commands = malloc(num_commands * sizeof(char**));
		int* command_lengths = malloc(num_commands * sizeof(int));
		int** pipes = malloc((num_commands-1) * sizeof(int*));

		i = 1;
		int first_token_of_last_cmd = 0;
		int cmd_i = 0;
		int is_pipe = 0;
		while(tokens[i] != NULL) {
			// check if the current token is a pipe symbol
			if(tokens[i][0] == '|' && tokens[i][1] == '\0')
				is_pipe = 1;
			 else
				is_pipe = 0;
			// make sure we don't have two pipes in a row
			if(is_pipe && i == first_token_of_last_cmd) {
				printf("Error: can't have two pipes in a row\n");
				return 1;
			}
			if(is_pipe) {
				// replace the pipe token from the array with a null pointer
				free(tokens[i]);
				tokens[i] = NULL;
				// create pipes
				pipes[cmd_i] = malloc(2);
				// store sequence of tokens leading up to the pipe into the commands array
				commands[cmd_i]= &tokens[first_token_of_last_cmd];
				command_lengths[cmd_i] = i - first_token_of_last_cmd;
				// prepare to do the same for the next command
				cmd_i++;
				first_token_of_last_cmd = i+1;
				pipe(pipes[cmd_i]);
			}
			i++;
		}
		// assemble final command
		commands[cmd_i]= &tokens[first_token_of_last_cmd];
		command_lengths[cmd_i] = i - first_token_of_last_cmd;

		// -- iterate through commands -- //
		// allocate arrays for strings of files for io redirection
		char** infiles = malloc(num_commands * sizeof(char*));
		char** outfiles = malloc(num_commands * sizeof(char*));
		// fill the arrays, and remove the redirection tokens from the command token arrays
		for(cmd_i = 0; cmd_i < num_commands; cmd_i++) {
			// look for file redirection operators
			//char** pure_command = malloc((command_lengths[cmd_i] + 1) * sizeof(char*));
			int pure_command_token_i = 0;
			infiles[cmd_i] = NULL;
			outfiles[cmd_i] = NULL;
			// iterate through tokens within the current command
			i = 0;
			for(; i < command_lengths[cmd_i]; i++) {
				// check for input redirection
				if(commands[cmd_i][i][0] == '<' && commands[cmd_i][i][1] == '\0') {
					// make sure there's a token after this
					if(commands[cmd_i][i+1] == NULL || i-1 == command_lengths[cmd_i]) {
						printf("Error: need to specify file for input redirection\n");
						return 1;
					} else {
						if(infiles[cmd_i] != NULL)
							printf("Warning: ignoring additional stdin redirect operator\n");
						else
							infiles[cmd_i] = commands[cmd_i][i+1];
						// skip the token that gives the filename
						i++;
					}
				} else
				// check for output redirection
				if(commands[cmd_i][i][0] == '>' && commands[cmd_i][i][1] == '\0') {
					// make sure there's a token after this
					if(commands[cmd_i][i+1] == NULL || i-1 == command_lengths[cmd_i]) {
						printf("Error: need to specify file for output redirection\n");
						return 1;
					} else {
						if(outfiles[cmd_i] != NULL)
							printf("Warning: ignoring additional stdout redirect operator\n");
						else
							outfiles[cmd_i] = commands[cmd_i][i+1];
						// skip the token that gives the filename
						i++;
					}
				} else {
					// add next non-redirect token to pure_command
					commands[pure_command_token_i] = commands[cmd_i];
					pure_command_token_i++;
				}
				//commands[cmd_i] = pure_command;
			}
			// end command here
			commands[pure_command_token_i] = NULL;
		}

		// fork and execute each file
		for(cmd_i = 0; cmd_i < num_commands; cmd_i++) {
			// fork
			int childPID = fork();
			if(childPID == 0) { // child process
				// redirect stdin to file, if necessary
				if(infiles[cmd_i] != NULL) {
					// make sure we're the first command in the pipeline
					if(cmd_i > 0) {
						printf("Error: can't have input redirection after first command in pipeline\n");
						return 1;
					}
					int in_fd = open(infiles[cmd_i], O_RDONLY);
					if(dup2(in_fd, 0) == -1)
						printf("Error 1: %s\n", strerror(errno));
				} else {
					if(pipes[cmd_i] != NULL) {
						dup2(pipes[cmd_i][0], 0);
						close(pipes[cmd_i][1]);
					}
				}

				// redirect stdout to file, if necessary
				if(outfiles[cmd_i] != NULL) {
					// make sure we're the last command in the pipeline
					if(cmd_i < num_commands - 1) {
						printf("Error: can't have output redirection before last command in pipeline\n");
						return 1;
					}
					int out_fd = open(outfiles[cmd_i], O_WRONLY | O_CREAT);
					if(dup2(out_fd, 1) == -1)
						printf("Error 2: %s\n", strerror(errno));
				} else {
					if(pipes[cmd_i] != NULL) {
						dup2(pipes[cmd_i][1], 1);
						close(pipes[cmd_i][0]);
					}
				}

				// execute program
				if(execvp(commands[cmd_i][0], commands[cmd_i]) == -1) {
					printf("Error 3: %s\n", strerror(errno));
					return 1;
				}
			} else { // parent process
				// wait for child to terminate, if we're supposed to
				if(!no_wait)
					waitpid(childPID, NULL, 0);
			}
		}

	}
	return 0;
}

/* 
  *reads a single line from terminal
 */
char* readLine() {

	// read actual line of input from terminal
	char* line = (char*) malloc( MAX_TOKEN_LENGTH+1 );
	if(getline(&line, &MAX_TOKEN_LENGTH, stdin) > MAX_TOKEN_LENGTH) {
		printf("WARNING: line is longer than %d characters!\n", (int)MAX_TOKEN_LENGTH);
		return NULL;
	}

	return line;
}

/* 
  *lex the line read into an array of tokens
 */
char** lex(char* line) {

	// allocate memory for array of pointers to arrays of characters (list of tokens)
	char** tokens = (char**) malloc( MAX_NUM_TOKENS * sizeof(char*) );
	int i;
	for (i=0; i<MAX_NUM_TOKENS; i++) {
		tokens[i] = (char*) malloc( MAX_TOKEN_LENGTH );
	}

	// take each token from line and add it to next spot in array of tokens
	i=0;
	char* curToken = strtok(line, " \n"); // find the first token
	while (curToken != NULL && i<MAX_NUM_TOKENS) {
		strcpy(tokens[i++], curToken); // store the token into the array
		curToken = strtok(NULL, " \n"); // find the next token
	}

	// check if we quit because of going over allowed word limit
	if (i == MAX_NUM_TOKENS) {
		printf( "WARNING: line contains more than %d tokens!\n", (int)MAX_NUM_TOKENS ); 
	} 
	else
		tokens[i] = NULL;

	// return the list of tokens
	return tokens;
}

