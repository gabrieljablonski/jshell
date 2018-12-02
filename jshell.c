/***************************************************************************//**
  @file         jshell.c
  @author       Gabriel Jablonski
  @brief        JShell, expanded upon LSH, by Stephen Brennan (github.com/brenns10/lsh/)
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <limits.h>
#include <netdb.h>
#include <pwd.h>

#define TOKEN_BUFFER_SIZE 32
#define MAX_WORD_SIZE 100

#define JSHELL_PROMPT ">> "
#define JSHELL_PIPE "|"
#define JSHELL_LINE_BUFFER_SIZE 1024
#define JSHELL_GENERIC_LIMIT 1024
#define JSHELL_EXIT_CODE 27
#define JSHELL_SUCCESS 0
#define JSHELL_FAILED 1

void raise_error(char* message);
void free_double_pointer(void** ptr);
int is_delimiter(char c);
char** split_line(char* line);
void print_prompt();
void jshell_loop();
char* jshell_read_line();
int jshell_run(char** args);
int jshell_cd(char **args);
int jshell_help(char **args);
int jshell_exit(char **args);
int jshell_exec_pipe(char **left_args, char **right_args);
int jshell_pipe(char **args);
int jshell_exec(char **args);
void init();
int main(int argc, char** argv);

struct Builtin {
    char* name;
    int (*func) (char**);
};

struct Builtin builtins[] = {
    { "cd", &jshell_cd },
    { "help", &jshell_help },
    { "exit", &jshell_exit }
};

int jshell_num_builtins() {
  return sizeof(builtins)/sizeof(struct Builtin);
}

// CONTROL UTILITIES

void raise_error(char* message){
    fprintf(stderr, "jshell: Error occurred: %s\n", message);
    exit(EXIT_FAILURE);
}


void free_double_pointer(void** ptr){
    int i;
    
    for(i=0; ptr[i]!=NULL; i++){
        free(ptr[i]);
    }
    free(ptr);
} 

// END CONTROL UTILITIES

// PARSING

int is_delimiter(char c){

    if(c == ' '  ||
       c == '\t' ||
       c == '\r' ||
       c == '\n' ||
       c == '\a')
    {
        return 1;
    }

    return 0;
}

char** split_line(char *line){
    int buffer_size = TOKEN_BUFFER_SIZE;
    int current_token = 0;

    int in_quotes = 0;
    int i, j;

    char** tokens = malloc(buffer_size*sizeof(char*));
    if(!tokens){
        raise_error("Failed allocation of `tokens`.");
    }

    char* token = malloc(MAX_WORD_SIZE*sizeof(char));
    if(!token){
        raise_error("Failed allocation of `token`.");
    }

    for(i=0, j=0;; i++){
        if(line[i] == '\0')
            goto End_token;

        if(line[i] == '"'){

            if(!in_quotes){
                in_quotes = 1;
                continue;
            }
            else{
                if(!is_delimiter(line[i+1]) && line[i+1]!='\0'){
                    raise_error("Expected delimiter after end quote.");
                }

                in_quotes = 0;
                i++;  // skip to delimiter after end quote
                goto End_token;
            }
        }

        // if character is not delimiter, add to token and move on
        if(!is_delimiter(line[i]) || in_quotes){
            token[j] = line[i];
            j++;
        }
        else{
            End_token:
            
            if(current_token >= buffer_size){
                buffer_size += TOKEN_BUFFER_SIZE;
                tokens = realloc(tokens, buffer_size*sizeof(char*));
                if(!tokens) {
                    raise_error("Failed allocation of `tokens`.");
                }
            }
            
            if(!j)
            {
                // if token is empty and EOF, just end parsing
                if(line[i]=='\0')  
                    break;
                // if not, skip to next character
                else  
                    continue;
            }

            token[j] = '\0';
            tokens[current_token] = token;

            token = malloc(MAX_WORD_SIZE*sizeof(char));
            if(!token){
                raise_error("Failed allocation of `token`.");
            }

            j = 0;
            current_token++;
            
            if(line[i]=='\0'){
                free(token);  // dispose newly created token
                break;
            }
        }

    }

    if(in_quotes){
        raise_error("Parsing ended unexpectedly.");
    }

    tokens[current_token] = NULL;
    return tokens;
}

// END PARSING

// JSHELL

void show_prompt(){
    char cwd[PATH_MAX];
    struct passwd* pwd;
    char hostname[JSHELL_GENERIC_LIMIT];
    struct hostent* h;
    
    if(getcwd(cwd, sizeof(cwd)) == NULL){
        perror("getcwd() error");
    }
    
    pwd = getpwuid(getuid());
    
    gethostname(hostname, sizeof(hostname));
    h = gethostbyname(hostname);
    
    printf("\n~%s@%s:%s ", pwd->pw_name, h->h_name, cwd);
    printf(JSHELL_PROMPT);
    
    // free(h)?
}

// INPUT

void jshell_loop(){
    char *line;
    char **args;

    int ret_code;

    do {
        show_prompt();
        
        line = jshell_read_line();
        args = split_line(line);

        ret_code = jshell_exec(args);

        free(line);
        free_double_pointer((void**)args);
    } while(ret_code!=JSHELL_EXIT_CODE);
}

char* jshell_read_line(){
    int buffer_size = JSHELL_LINE_BUFFER_SIZE;
    int position = 0;
    int c;
    
    char* line_buffer = malloc(buffer_size*sizeof(char));
    if(!line_buffer){
        raise_error("Failed allocation of `line_buffer`.");
    }
    
    for(;;position++){
        if(position >= buffer_size){
            buffer_size += JSHELL_LINE_BUFFER_SIZE;
            line_buffer = realloc(line_buffer, buffer_size);
            if(!line_buffer){
                raise_error("Failed allocation of `line_buffer`.");
            }    
        }
        
        c = getchar();
        
        if(c==EOF || c=='\n'){
            line_buffer[position] = '\0';
            return line_buffer;
        }
        else{
            line_buffer[position] = c;
        }
    }
    
}

// END INPUT

// COMMANDS

int jshell_run(char **args){
    pid_t pid, wpid;
    int status;
    
    pid = fork();
    if(pid==0){
        if(execvp(args[0], args) < 0){
            char* err_msg = (char*)malloc(JSHELL_GENERIC_LIMIT*sizeof(char));
            sprintf(err_msg, "jshell(\"%s\")", args[0]);
            perror(err_msg);
        }
        exit(EXIT_FAILURE);
    }
    else if(pid < 0){
        perror("jshell");
    }
    else{
        do{
            wpid = waitpid(pid, &status, WUNTRACED);
        } while(!WIFEXITED(status) && !WIFSIGNALED(status));
        
    }
    
    return JSHELL_SUCCESS;
}

int jshell_cd(char **args){
    if(args[1] == NULL) {
        raise_error("Argument expected for 'cd' command");
    } 
    else {
        if(chdir(args[1]) != 0) {
            perror("jshell");
        }
    }
    return JSHELL_SUCCESS;
}

int jshell_help(char **args){
    int i;
    printf("\nJShell\n\n");
    printf("--Simple piping can be done through '|' character.\n");
    printf("Usage: \"cmd1 arg0 arg1 ... | cmd2 arg0 arg1 ...\" (Support only for piping between 2 programs).\n\n");
    printf("--Double quotes can be used for arguments containing delimiters.\n\n");
    printf("The following commands are built in:\n");

    int n_builtins = jshell_num_builtins();
    for (i = 0; i < n_builtins; i++) {
        printf("> %s\n", builtins[i].name);
    }
    printf("\n");
    
    return JSHELL_SUCCESS;
}

int jshell_exit(char **args){
  return JSHELL_EXIT_CODE;
}

int jshell_exec_pipe(char **left_args, char **right_args){
    int pipefd[2];  //0: read; 1: write
    pid_t p1, p2, wpid1, wpid2;
    int status1, status2;
    
    if(pipe(pipefd) < 0) {
        fprintf(stderr, "jshell: Pipe could not be initialized.\n");
        return JSHELL_FAILED;
    }
    p1 = fork();
    if(p1 < 0) {
        fprintf(stderr, "jshell: Fork failed.\n");
        return JSHELL_FAILED;
    }
    
    else if(p1 == 0) {
        // close read end
        close(pipefd[0]);
        
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        
        if(execvp(left_args[0], left_args) < 0) {
            char* err_msg = (char*)malloc(JSHELL_GENERIC_LIMIT*sizeof(char));
            sprintf(err_msg, "Failed execution of '%s'", left_args[0]);
            perror(err_msg);
        }
        exit(EXIT_FAILURE);
    }
    else {  // parent
        p2 = fork();
        
        if(p2 < 0) {
            fprintf(stderr, "jshell: Fork failed.\n");
            return JSHELL_FAILED;
        }
  
        else if(p2 == 0) {
            // close write end
            close(pipefd[1]);
            
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
            
            if(execvp(right_args[0], right_args) < 0) {
                char* err_msg = (char*)malloc(JSHELL_GENERIC_LIMIT*sizeof(char));
                sprintf(err_msg, "Failed execution of '%s'", right_args[0]);
                perror(err_msg);
            }
            exit(EXIT_FAILURE);
        } 
        else{			
			do{
				wpid1 = waitpid(p1, &status1, WUNTRACED);
			} while(!WIFEXITED(status1) && !WIFSIGNALED(status1));
			
			do{
				wpid2 = waitpid(p2, &status2, WUNTRACED);
			} while(!WIFEXITED(status2) && !WIFSIGNALED(status2));
        }
    }
    
    return JSHELL_SUCCESS;
}

int jshell_pipe(char **args){
    char **left_args;
    char **right_args;
    
    int i, j;
    int sizeof_left = 0;
    int sizeof_right;
    
    for(i=1; args[i]!=NULL; i++){
        if(args[i][0]==JSHELL_PIPE[0]){
            sizeof_left = i + 1;
        }
    }
    sizeof_right = i - sizeof_left + 1;
    
    // Shouldn't ever happen
    if(sizeof_left == 0 || sizeof_right == 0){
        return 1;
    }    
    
    left_args = malloc(sizeof_left*sizeof(char*));
    if(!left_args){
        raise_error("Failed allocation of `left_args`.");
    }
    
    right_args = malloc(sizeof_right*sizeof(char*));
    if(!right_args){
        raise_error("Failed allocation of `right_args`.");
    }
    
    for(i=0, j=0; args[i]!=NULL; i++){
        if(args[i][0]==JSHELL_PIPE[0])
            continue;
        if(i < sizeof_left)
            left_args[i] = args[i];
        else{
            right_args[j] = args[i];
            j++;
		}
    }
    left_args[sizeof_left-1] = NULL;    // NULL terminated arrays
    right_args[sizeof_right-1] = NULL;
    
    return jshell_exec_pipe(left_args, right_args);
}

int jshell_exec(char **args){
    int i;

    if(args[0] == NULL) {
        // empty command
        return JSHELL_SUCCESS;
    }
    
    for(i=0; args[i]!=NULL; i++){
        if(args[i][0]==JSHELL_PIPE[0]){
            if(strlen(args[i])>1 || i==0){
                fprintf(stderr, "jshell: Syntax error for '|'.\n");
                return JSHELL_FAILED;
            }
            if(args[i+1]==NULL){
                fprintf(stderr, "jshell: Right command expected for piping.\n");
                return JSHELL_FAILED;
            }
            return jshell_pipe(args);
        }
        
    }

    for (i = 0; i<jshell_num_builtins(); i++) {
        if(strcmp(args[0], builtins[i].name) == 0) {
            // run builtin, if available
            return (*builtins[i].func)(args);
        }
    }
    // run external, if no builtin match
    return jshell_run(args);
}

// END COMMANDS

// END JSHELL

void init(){
    return;
}

int main(int argc, char** argv)
{
    init();
    jshell_loop();
    
    return EXIT_SUCCESS;
}
