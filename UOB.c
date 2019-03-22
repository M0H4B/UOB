#include <stdio.h>
#include <stdlib.h>  // free,
#include <unistd.h>  //getcwd ,
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <glob.h>
#include <sys/wait.h> // waitpid,WNOHANG,WUNTRACED,WCONTINUED
#include <readline/readline.h>
#include <readline/history.h>  // add_history


#define NR_JOBS 20
#define PATH_BUFSIZE 1024
#define COMMAND_BUFSIZE 1024
#define TOKEN_BUFSIZE 64
#define TOKEN_DELIMITERS " \t\r\n\a"
#define clear() printf("\033[H\033[J")
#define BACKGROUND_EXECUTION 0
#define FOREGROUND_EXECUTION 1
#define PIPELINE_EXECUTION 2

#define COMMAND_EXTERNAL 0
#define COMMAND_EXIT 1
#define COMMAND_CD 2
#define COMMAND_JOBS 3
#define COMMAND_KILL 6
#define COMMAND_HELP 9
#define COMMAND_CTRL_C 10

#define STATUS_RUNNING 0
#define STATUS_DONE 1
#define STATUS_SUSPENDED 2
#define STATUS_CONTINUED 3
#define STATUS_TERMINATED 4

#define COLOR_NONE "\033[m"
#define COLOR_RED "\033[1;37;41m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_CYAN "\033[0;36m"
#define COLOR_GREEN "\033[0;32;32m"
#define COLOR_GRAY "\033[1;30m"

const char* STATUS_STRING[] = {
    "running",
    "done",
    "suspended",
    "continued",
    "terminated"
};


// define list of process to store all process
struct process {
    char *command;
    int argc;
    char **argv;
    char *input_path;
    char *output_path;
    pid_t pid;
    int type;
    int status;
    struct process *next;
};


// class job contains
// job id num
// root process
// command text
// process id
// and mode
struct job {
    int id;
    struct process *root;
    char *command;
    pid_t pgid;
    int mode;
};

// we use this to store shell information
struct shell_info {
    char cur_user[TOKEN_BUFSIZE];
    char cur_dir[PATH_BUFSIZE];
    char pw_dir[PATH_BUFSIZE];
    struct job *jobs[NR_JOBS + 1];
};

struct shell_info *shell;

int get_job_id_by_pid(int pid) {
    int i;
    struct process *proc;

    for (i = 1; i <= NR_JOBS; i++) {
        if (shell->jobs[i] != NULL) {
            for (proc = shell->jobs[i]->root; proc != NULL; proc = proc->next) {
                if (proc->pid == pid) {
                    return i;
                }
            }
        }
    }

    return -1;
}


// get job by id it's will return an object of job
struct job* get_job_by_id(int id) {
    if (id > NR_JOBS) {
        return NULL;
    }

    return shell->jobs[id];
}

// get process id by job id
int get_pgid_by_job_id(int id) {
    struct job *job = get_job_by_id(id);

    if (job == NULL) {
        return -1;
    }

    return job->pgid;
}

// this will return number of count of process
// for each process not done
int get_proc_count(int id) {
    if (id > NR_JOBS || shell->jobs[id] == NULL) {
        return -1;
    }

    int count = 0;
    struct process *proc;
    for (proc = shell->jobs[id]->root; proc != NULL; proc = proc->next) {
        if ( proc->status != STATUS_DONE) {
            count++;
        }
    }

    return count;
}

int get_next_job_id() {
    int i;

    for (i = 1; i <= NR_JOBS; i++) {
        if (shell->jobs[i] == NULL) {
            return i;
        }
    }

    return -1;
}



int print_processes_of_job(int id) {
    if (id > NR_JOBS || shell->jobs[id] == NULL) {
        return -1;
    }

    printf("[%d]", id);

    struct process *proc;
    for (proc = shell->jobs[id]->root; proc != NULL; proc = proc->next) {
        printf(" %d", proc->pid);
    }
    printf("\n");

    return 0;
}
// to print all bg jobs
//example
//----------------------------
//NUM     PID 	STATUS	CMD
//[1]	13387	running	sleep 100
int print_job_status(int id) {
    if (id > NR_JOBS || shell->jobs[id] == NULL) {
        return -1;
    }
    
    printf("[%d]", id);

    struct process *proc;
    for (proc = shell->jobs[id]->root; proc != NULL; proc = proc->next) {
        printf("\t%d\t%s\t%s", proc->pid,
            STATUS_STRING[proc->status], proc->command);
        if (proc->next != NULL) {
            printf("|\n");
        } else {
            printf("\n");
        }
    }

    return 0;
}

int release_job(int id) {
    if (id > NR_JOBS || shell->jobs[id] == NULL) {
        return -1;
    }

    struct job *job = shell->jobs[id];
    struct process *proc, *tmp;
    for (proc = job->root; proc != NULL; ) {
        tmp = proc->next;
        free(proc->command);
        free(proc->argv);
        free(proc->input_path);
        free(proc->output_path);
        free(proc);
        proc = tmp;
    }

    free(job->command);
    free(job);

    return 0;
}


// insert new job to list of jobs
int insert_job(struct job *job) {
    int id = get_next_job_id();

    if (id < 0) {
        return -1;
    }

    job->id = id;
    shell->jobs[id] = job;
    return id;
}
// remove existing job from the list
int remove_job(int id) {
    if (id > NR_JOBS || shell->jobs[id] == NULL) {
        return -1;
    }

    release_job(id);
    shell->jobs[id] = NULL;

    return 0;
}
// a func  will return 1 if job is not complete & 0 if it's complete or doesn't exist
// using job id number
int is_job_completed(int id) {
    if (id > NR_JOBS || shell->jobs[id] == NULL) {
        return 0;
    }

    struct process *proc;
    for (proc = shell->jobs[id]->root; proc != NULL; proc = proc->next) {
        if (proc->status != STATUS_DONE) {
            return 0;
        }
    }

    return 1;
}

//this func will set a status to job by it's id
// STATUS_RUNNING 0
// STATUS_DONE 1
// STATUS_SUSPENDED 2
// STATUS_CONTINUED 3
// STATUS_TERMINATED 4
int set_process_status(int pid, int status) {
    int i;
    struct process *proc;

    for (i = 1; i <= NR_JOBS; i++) {
        if (shell->jobs[i] == NULL) {
            continue;
        }
        for (proc = shell->jobs[i]->root; proc != NULL; proc = proc->next) {
            if (proc->pid == pid) {
                proc->status = status;
                return 0;
            }
        }
    }

    return -1;
}
// change job status
int set_job_status(int id, int status) {
    if (id > NR_JOBS || shell->jobs[id] == NULL) {
        return -1;
    }

    int i;
    struct process *proc;

    for (proc = shell->jobs[id]->root; proc != NULL; proc = proc->next) {
        if (proc->status != STATUS_DONE) {
            proc->status = status;
        }
    }

    return 0;
}


int wait_for_job(int id) {
    if (id > NR_JOBS || shell->jobs[id] == NULL) {
        return -1;
    }

    int proc_count = get_proc_count(id);
    int wait_pid = -1, wait_count = 0;
    int status = 0;

    do {
        wait_pid = waitpid(-shell->jobs[id]->pgid, &status, WUNTRACED);
        wait_count++;

        if (WIFEXITED(status)) {
            set_process_status(wait_pid, STATUS_DONE);
        } else if (WIFSIGNALED(status)) {
            set_process_status(wait_pid, STATUS_TERMINATED);
        } else if (WSTOPSIG(status)) {
            status = -1;
            set_process_status(wait_pid, STATUS_SUSPENDED);
            if (wait_count == proc_count) {
                print_job_status(id);
            }
        }
    } while (wait_count < proc_count);

    return status;
}

int get_command_type(char *command) {
    if (strcmp(command, "exit") == 0) {
        return COMMAND_EXIT;
    } else if (strcmp(command, "cd") == 0) {
        return COMMAND_CD;
    } else if (strcmp(command, "jobs") == 0) {
        return COMMAND_JOBS;
    }else if (strcmp(command, "kill") == 0) {
        return COMMAND_KILL;
    }else if (strcmp(command, "help") == 0) {
        return COMMAND_HELP;
    }else if (strcmp(command, "^C") == 0 || strcmp(command, "^c") == 0) {
        return COMMAND_CTRL_C;
    } else {
        return COMMAND_EXTERNAL;
    }
}

char* helper_strtrim(char* line) {
    char *head = line, *tail = line + strlen(line);

    while (*head == ' ') {
        head++;
    }
    while (*tail == ' ') {
        tail--;
    }
    *(tail + 1) = '\0';

    return head;
}

void uob_update_cwd_info() {
    getcwd(shell->cur_dir, sizeof(shell->cur_dir));
}

//change direc
int uob_cd(int argc, char** argv) {
    if (argc == 1) {
        chdir(shell->pw_dir);
        uob_update_cwd_info();
        return 0;
    }

    if (chdir(argv[1]) == 0) {
        uob_update_cwd_info();
        return 0;
    } else {
        printf("uob: cd %s: No such file or directory\n", argv[1]);
        return 0;
    }
}

int uob_jobs(int argc, char **argv) {
    int i;
    
    printf("----------------------------\n");
	printf("NUM     PID 	STATUS	CMD\n");	
    for (i = 0; i < NR_JOBS; i++) {
        if (shell->jobs[i] != NULL) {

            print_job_status(i);
        }
    }

    return 0;
}

// killing jobs
//example
//kill 1
//kill 2 4

int uob_kill(int argc, char **argv) {
    if (argc < 2) {
	        printf("Usage: kill <pid> [<pid> ...]\n");
	        return -1;
	    }
	    pid_t pid;
	    int job_id = -1;
   		for (int i = 1; i < argc; ++i)
   		{
   			job_id = atoi(argv[i]);
	        pid = get_pgid_by_job_id(job_id);
	  
	        if (pid < 0) {
	            printf("uob: kill %s: no such job\n", argv[i]);
	            continue;
	        }
	        pid = -pid;
		    if (kill(pid, SIGKILL) < 0) {
		        printf("uob: kill %d: job not found\n", pid);
		        continue;
		    }
		    if (job_id > 0) {
		        set_job_status(job_id, STATUS_TERMINATED);
		        print_job_status(job_id);
		        if (wait_for_job(job_id) >= 0) {
		            remove_job(job_id);
		        }
		    }
   			
   		}
	    
        return 0;
	    
	
    
}
// exit without errors
int uob_exit() {
    printf("bye!\n");
    exit(0);
}


//this function well check process status if it's done well print a message
// for example  when i lunch new job like sleep 10  and i pressed ^z  the process state 'll change to suspended and it well print a message
void check_zombie() {
    int status, pid;
    while ((pid = waitpid(-1, &status, WNOHANG|WUNTRACED|WCONTINUED)) > 0) {
        if (WIFEXITED(status)) {
            set_process_status(pid, STATUS_DONE);

        } else if (WIFSTOPPED(status)) {
            set_process_status(pid, STATUS_SUSPENDED);

        } else if (WIFCONTINUED(status)) {
            set_process_status(pid, STATUS_CONTINUED);
        }

        int job_id = get_job_id_by_pid(pid);
        if (job_id > 0 && is_job_completed(job_id)) {
            print_job_status(job_id);
            remove_job(job_id);
        }
    }
}


// Help command builtin
void openHelp()
{
    printf("\n***WELCOME TO MY SHELL HELP 'xx'***"
        "\nGNU UOB, version 1.0.0"
        "\nList of Commands supported:"
        "\n>cd"
        "\n>ls"
        "\n>exit"
        "\n>all other general commands available in UNIX shell"
        "\n>pipe handling"
        "\n>improper space handling\n");
 
}

// execute command by type
int uob_execute_builtin_command(struct process *proc) {
    int status = 1;

    switch (proc->type) {
        case COMMAND_EXIT:
            uob_exit();
            break;
        case COMMAND_CD:
            uob_cd(proc->argc, proc->argv);
            break;
        case COMMAND_JOBS:
            uob_jobs(proc->argc, proc->argv);
            break;
        case COMMAND_KILL:
            uob_kill(proc->argc, proc->argv);
            break;
        case COMMAND_HELP:
        		openHelp();
        	break; 
        case COMMAND_CTRL_C:
        		uob_exit();
        	break;  
        default:
            status = 0;
            break;
    }

    return status;
}

int uob_launch_process(struct job *job, struct process *proc, int in_fd, int out_fd, int mode) {
    proc->status = STATUS_RUNNING;
    if (proc->type != COMMAND_EXTERNAL && uob_execute_builtin_command(proc)) {
        return 0;
    }

    pid_t childpid;
    int status = 0;

    childpid = fork();

    if (childpid < 0) {
        return -1;
    } else if (childpid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        proc->pid = getpid();
        if (job->pgid > 0) {
            setpgid(0, job->pgid);
        } else {
            job->pgid = proc->pid;
            setpgid(0, job->pgid);
        }

        if (in_fd != 0) {
            dup2(in_fd, 0);
            close(in_fd);
        }

        if (out_fd != 1) {
            dup2(out_fd, 1);
            close(out_fd);
        }

        if (execvp(proc->argv[0], proc->argv) < 0) {
            printf("uob: %s: command not found\n", proc->argv[0]);
            exit(0);
        }

        exit(0);
    } else {
        proc->pid = childpid;
        if (job->pgid > 0) {
            setpgid(childpid, job->pgid);
        } else {
            job->pgid = proc->pid;
            setpgid(childpid, job->pgid);
        }

        if (mode == FOREGROUND_EXECUTION) {
            tcsetpgrp(0, job->pgid);
            status = wait_for_job(job->id);
            signal(SIGTTOU, SIG_IGN);
            tcsetpgrp(0, getpid());
            signal(SIGTTOU, SIG_DFL);
        }
    }

    return status;
}

//start new job
int uob_launch_job(struct job *job) {

    struct process *proc;
    int status = 0, in_fd = 0, fd[2], job_id = -1;
    check_zombie();
    if (job->root->type == COMMAND_EXTERNAL) {
        job_id = insert_job(job);
    }

    for (proc = job->root; proc != NULL; proc = proc->next) {
        if (proc == job->root && proc->input_path != NULL) {
            in_fd = open(proc->input_path, O_RDONLY);
            if (in_fd < 0) {
                printf("uob: no such file or directory: %s\n", proc->input_path);
                remove_job(job_id);
                return -1;
            }
        }
        if (proc->next != NULL) {
            pipe(fd);
            status = uob_launch_process(job, proc, in_fd, fd[1], PIPELINE_EXECUTION);
            close(fd[1]);
            in_fd = fd[0];
        } else {
            int out_fd = 1;
            if (proc->output_path != NULL) {
                out_fd = open(proc->output_path, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
                if (out_fd < 0) {
                    out_fd = 1;
                }
            }
            status = uob_launch_process(job, proc, in_fd, out_fd, job->mode);
        }
    }

    if (job->root->type == COMMAND_EXTERNAL) {
        if (status >= 0 && job->mode == FOREGROUND_EXECUTION) {
            remove_job(job_id);
        } else if (job->mode == BACKGROUND_EXECUTION) {
            print_processes_of_job(job_id);
        }
    }

    return status;
}

struct process* uob_parse_command_segment(char *segment) {
    int bufsize = TOKEN_BUFSIZE;
    int position = 0;
    char *command = strdup(segment);
    char *token;
    char **tokens = (char**) malloc(bufsize * sizeof(char*));

    if (!tokens) {
        fprintf(stderr, "uob: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(segment, TOKEN_DELIMITERS);
    while (token != NULL) {
        glob_t glob_buffer;
        int glob_count = 0;
        if (strchr(token, '*') != NULL || strchr(token, '?') != NULL) {
            glob(token, 0, NULL, &glob_buffer);
            glob_count = glob_buffer.gl_pathc;
        }

        if (position + glob_count >= bufsize) {
            bufsize += TOKEN_BUFSIZE;
            bufsize += glob_count;
            tokens = (char**) realloc(tokens, bufsize * sizeof(char*));
            if (!tokens) {
                fprintf(stderr, "uob: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        if (glob_count > 0) {
            int i;
            for (i = 0; i < glob_count; i++) {
                tokens[position++] = strdup(glob_buffer.gl_pathv[i]);
            }
            globfree(&glob_buffer);
        } else {
            tokens[position] = token;
            position++;
        }

        token = strtok(NULL, TOKEN_DELIMITERS);
    }

    int i = 0, argc = 0;
    char *input_path = NULL, *output_path = NULL;
    while (i < position) {
        if (tokens[i][0] == '<' || tokens[i][0] == '>') {
            break;
        }
        i++;
    }
    argc = i;

    for (; i < position; i++) {
        if (tokens[i][0] == '<') {
            if (strlen(tokens[i]) == 1) {
                input_path = (char *) malloc((strlen(tokens[i + 1]) + 1) * sizeof(char));
                strcpy(input_path, tokens[i + 1]);
                i++;
            } else {
                input_path = (char *) malloc(strlen(tokens[i]) * sizeof(char));
                strcpy(input_path, tokens[i] + 1);
            }
        } else if (tokens[i][0] == '>') {
            if (strlen(tokens[i]) == 1) {
                output_path = (char *) malloc((strlen(tokens[i + 1]) + 1) * sizeof(char));
                strcpy(output_path, tokens[i + 1]);
                i++;
            } else {
                output_path = (char *) malloc(strlen(tokens[i]) * sizeof(char));
                strcpy(output_path, tokens[i] + 1);
            }
        } else {
            break;
        }
    }

    for (i = argc; i <= position; i++) {
        tokens[i] = NULL;
    }

    struct process *new_proc = (struct process*) malloc(sizeof(struct process));
    new_proc->command = command;
    new_proc->argv = tokens;
    new_proc->argc = argc;
    new_proc->input_path = input_path;
    new_proc->output_path = output_path;
    new_proc->pid = -1;
    new_proc->type = get_command_type(tokens[0]);
    new_proc->next = NULL;
    return new_proc;
}

struct job* uob_parse_command(char *line) {
    line = helper_strtrim(line);
    char *command = strdup(line);

    struct process *root_proc = NULL, *proc = NULL;
    char *line_cursor = line, *c = line, *seg;
    int seg_len = 0, mode = FOREGROUND_EXECUTION;

    if (line[strlen(line) - 1] == '&') {
        mode = BACKGROUND_EXECUTION;
        line[strlen(line) - 1] = '\0';
    }

    while (1) {
        if (*c == '\0' || *c == '|') {
            seg = (char*) malloc((seg_len + 1) * sizeof(char));
            strncpy(seg, line_cursor, seg_len);
            seg[seg_len] = '\0';

            struct process* new_proc = uob_parse_command_segment(seg);
            if (!root_proc) {
                root_proc = new_proc;
                proc = root_proc;
            } else {
                proc->next = new_proc;
                proc = new_proc;
            }

            if (*c != '\0') {
                line_cursor = c;
                while (*(++line_cursor) == ' ');
                c = line_cursor;
                seg_len = 0;
                continue;
            } else {
                break;
            }
        } else {
            seg_len++;
            c++;
        }
    }

    struct job *new_job = (struct job*) malloc(sizeof(struct job));
    new_job->root = root_proc;
    new_job->command = command;
    new_job->pgid = -1;
    new_job->mode = mode;
    return new_job;
}

// looks like PS1 in bash :P
void uob_print_promt() {
    printf(COLOR_CYAN "%s" COLOR_NONE "@" COLOR_YELLOW "%s" COLOR_NONE ":%s", shell->cur_user, shell->cur_dir,COLOR_RED "UOB#" COLOR_NONE " ");
}

void uob_print_welcome() {
    printf("Hi xx :P\n" );
}

void uob_loop() {
    char *line;
    struct job *job;
    int status = 1;

    while (1) {
        uob_print_promt();
	    line = readline("");
	    if (strlen(line) != 0) {
	        add_history(line);

	    }
        if (strlen(line) == 0) {
            check_zombie();
            continue;
        }
        job = uob_parse_command(line);
        status = uob_launch_job(job);
        free(line);
    }
}


void sigint_handler(int signal) {
    printf("\n");
}
void uob_init() {
    struct sigaction sigint_action = {
        .sa_handler = &sigint_handler,
        .sa_flags = 0
    };
    sigemptyset(&sigint_action.sa_mask);
    sigaction(SIGINT, &sigint_action, NULL);

    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    pid_t pid = getpid();
    setpgid(pid, pid);
    tcsetpgrp(0, pid);

    shell = (struct shell_info*) malloc(sizeof(struct shell_info));
    getlogin_r(shell->cur_user, sizeof(shell->cur_user));

    struct passwd *pw = getpwuid(getuid());
    strcpy(shell->pw_dir, pw->pw_dir); 

    int i;
    for (i = 0; i < NR_JOBS; i++) {
        shell->jobs[i] = NULL;
    }

    uob_update_cwd_info();
}



// UOB 'll start exec.. from here
int main(int argc, char **argv) {
    uob_init();
    uob_print_welcome();
    uob_loop();

    return EXIT_SUCCESS;
}

