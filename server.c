#include <sys/wait.h>
#include "server.h"
#include "cmd_node.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "util.h"
#include <sys/types.h>
#include <sys/stat.h>
#include "node_list.h"
#include <stdlib.h>
#include <stdio.h>
#include "parser.h"
#include "tokenizer.h"
#include "pipe_node.h"
#include "client_node.h"
#include "main.h"
#include "mq.h"
#include "global_pipe.h"
#include <signal.h>



extern char* last_line;

char* welcome_message = "****************************************\n** Welcome to the information server. **\n****************************************";

void _dbg_print_client_pip_list(client_node_t* client) {
    pipe_node_t* list = client->pipe_list;
    printf("[DBG] [%s]\n", client->name);

    while(list != NULL) {
        printf("[DBG] in: %d(%d), out: %d(%d), count: %d\n", list->in_fd, fcntl(list->in_fd, 1), list->out_fd, fcntl(list->out_fd, 1), list->count);
        list = list->next_node;
    }
    fflush(stdout);
}


void signal_handle() {
    ras_msg_t data = mq_recv_msg(TELL_MSG, -1, client_id);
    if(data.type != -1) {
        printf("%s", data.msg);
        fflush(stdout);
        client_signal();
    }
}

int serve(client_node_t* client) {

    client->pid = getpid();
    client_id = client->id;
    signal(SIGUSR1, signal_handle);

    int c;
    // replace stdin, stdout, stderr to client fd
    for(c=0; c<3; c++){
        dup2(client->client_sc_fd, c);
    }

    for(c=0; c<client->num_env; c++) {
        setenv(client->env[c], client->env_val[c], 1);
    }

    printf("%s\n", welcome_message);
    fflush(stdout);
    char broad_msg[1024];
    bzero(broad_msg, 1024);
    sprintf(broad_msg, "*** User '(no name)' entered from %s/%d. ***\n", client->ip, client->port);
    broad_cast(client, broad_msg);
    printf("%% ");
    fflush(stdout);

    while(1){
        cmd_node_t* cmd_node_list = NULL;
        parse_tokens(&cmd_node_list);
        int state;

        if(cmd_node_list != NULL) {

            // handle_from_user_node(&cmd_node_list);
            // process commands
            // despatch node to right palce
            while(cmd_node_list != NULL) {
                cmd_node_t* node_to_exec = pull_cmd_node(&cmd_node_list);
                if(node_to_exec->pipe_count == -1) {
                    break;
                }
                int errnum = exec_cmd_node(node_to_exec, client);
                if(errnum == -1) {
                    // command not found
                    printf("Unknown command: [%s].\n", node_to_exec->cmd);
                    fflush(stdout);
                    free_cmd_list(&cmd_node_list);
                } else if(errnum == -2) {
                    // pipe to me not exist
                    printf("*** Error: the pipe #%d->#%d does not exist yet. ***\n", node_to_exec->from_user_id, client->id);
                    fflush(stdout);
                    free_cmd_list(&cmd_node_list);
                } else if(errnum == -3) {
                    // pipe to other exist
                    printf("*** Error: the pipe #%d->#%d already exists. ***\n", client->id, node_to_exec->to_user_id);
                    fflush(stdout);
                } else if(errnum == -4) {
                    // logout
                    return -1;
                }
            }
        }
        printf("%% ");
        fflush(stdout);
    }
    return 0;
}

int exec_cmd_node(cmd_node_t* cmd_node, client_node_t* client) {

    int pipe_count = cmd_node->pipe_count;
    int pid = -1;
    int input_pipe_fd = -1;
    int output_pipe_fd = -1;

    if(strcmp(cmd_node->cmd, "printenv") == 0) {
        char* env_name = cmd_node->args[1];
        if(env_name != NULL) {
            char* env_val = getenv(env_name);
            printf("%s=%s\n", env_name, env_val);
            fflush(stdout);
        }
        decrease_all_pipe_node(client->pipe_list);
        return 0;
    } else if(strcmp(cmd_node->cmd, "setenv") == 0) {
        char* env_name = cmd_node->args[1];
        char* env_val = cmd_node->args[2];
        set_env_to_client_node(client, env_name, env_val);
        setenv(env_name, env_val, 1);
        decrease_all_pipe_node(client->pipe_list);
        return 0;
    } else if(strcmp(cmd_node->cmd, "exit") == 0) {
        char exit_msg[64];
        bzero(exit_msg, 64);
        sprintf(exit_msg, "*** User '%s' left. ***\n", client->name);
        broad_cast(client, exit_msg);
        // clean message queues
        mq_clean(client->id);
        // remove global pipes
        remove_global_pipe(client->id, client->id, 0);
        // remove client from client list
        remove_client_node(client);
        // return logout status
        return -4;
    } else if(strcmp(cmd_node->cmd, "who") == 0) {
        who(client);
        decrease_all_pipe_node(client->pipe_list);
        return 0;
    } else if(strcmp(cmd_node->cmd, "tell") == 0) {
        int r = tell(client, cmd_node->args[1], cmd_node->args[2]);
        if(r == -1) {
            printf("*** Error: user #%s does not exist yet. ***\n", cmd_node->args[1]);
            fflush(stdout);
        } else {
            decrease_all_pipe_node(client->pipe_list);
        }
        return 0;
    } else if(strcmp(cmd_node->cmd, "yell") == 0) {
        char message[11000];
        sprintf(message, "*** %s yelled ***: %s\n", client->name, cmd_node->args[1]);
        bzero(message, 11000);
        broad_cast(client, message);
        decrease_all_pipe_node(client->pipe_list);
        return 0;
    } else if(strcmp(cmd_node->cmd, "name") == 0) {
        char* name = cmd_node->args[1];
        if(check_name_exist(name) == 1) {
            printf("*** User '%s' already exists. ***\n", name);
            fflush(stdout);
        } else {
            set_client_name(client->id, name);
            strcpy(client->name, name);
            char msg[40];
            bzero(msg, 40);
            sprintf(msg, "*** User from %s/%d is named '%s'. ***\n", client->ip, client->port, client->name);
            broad_cast(client, msg);
            fflush(stdout);
        }
        decrease_all_pipe_node(client->pipe_list);
        return 0;
    }

    decrease_all_pipe_node(client->pipe_list);

    // get this process input source
    pipe_node_t* in_pipe_node = find_pipe_node_by_count(client->pipe_list, 0);
    if(in_pipe_node != NULL) {
        input_pipe_fd = in_pipe_node->in_fd;
        if(fcntl(in_pipe_node->out_fd, F_GETFD) != -1) {
            close(in_pipe_node->out_fd);
        }
        in_pipe_node->count--;
    }
    if(cmd_node->pipe_from_user == 1) {
        char data[2048];
        bzero(data, 2048);
        int r = pull_global_pipe_data(cmd_node->from_user_id, client->id, data);
        if(r == -1) {
            inscrease_all_pipe_node(client->pipe_list);
            return -2;
        }
        int g_pipe[2];
        pipe(g_pipe);
        write(g_pipe[1], r.msg, strlen(r.msg));
        close(g_pipe[1]);
        input_pipe_fd = g_pipe[0];
    }
    int tmp_global_pipe[2];

    // get this process output source
    pipe_node_t* out_pipe_node = find_pipe_node_by_count(client->pipe_list, pipe_count);
    if(out_pipe_node != NULL) {
        output_pipe_fd = out_pipe_node->out_fd;
    } else if(cmd_node->pipe_to_file == 1){
        output_pipe_fd = get_file_fd(cmd_node->filename);
    } else if(cmd_node->pipe_to_user == 1) {
        if(is_client_available(cmd_node->to_user_id) != 1) {
            printf("*** Error: user #%d does not exist yet. ***\n", cmd_node->to_user_id);
            fflush(stdout);
            close_unused_fd(client);
            return 0;
        } else if(is_global_pipe_exist(client->id, cmd_node->to_user_id) == 1) {
            close_unused_fd(client);
            return -3;
        }
        // global pipe
        // create global pipe node
        pipe(tmp_global_pipe);
        output_pipe_fd = tmp_global_pipe[1];

        // broad cast message
        // *** (name) (#<client id>) just piped '(command line)' to (receiver's name) (#<receiver's client_id>) ***
        char* msg_temp = "*** %s (#%d) just piped '%s' to %s (#%d) ***\n";
        char msg[128];
        bzero(msg, 128);
        char to_client_name[30];
        get_client_name(cmd_node->to_user_id, to_client_name);
        sprintf(msg, msg_temp, client->name, client->id, last_line, to_client_name, cmd_node->to_user_id);
        broad_cast(client, msg);
        fflush(stdout);
    } else if(cmd_node->pipe_count != 0) {
        int new_pipe_fd[2];
        pipe(new_pipe_fd);
        out_pipe_node = malloc(sizeof(pipe_node_t));
        out_pipe_node->count = cmd_node->pipe_count;
        out_pipe_node->in_fd = new_pipe_fd[0];
        out_pipe_node->out_fd = new_pipe_fd[1];
        out_pipe_node->next_node = NULL;
        insert_pipe_node(&(client->pipe_list), out_pipe_node);

        output_pipe_fd = new_pipe_fd[1];
    }


    pid = fork();
    if(pid == 0) {
        if(input_pipe_fd != -1) {
            // not use stdin
            close(0);
            dup(input_pipe_fd);
            close(input_pipe_fd);
        }

        // out
        if(out_pipe_node != NULL) {
            close(1);
            dup(out_pipe_node->out_fd);
            close(out_pipe_node->out_fd);
        } else if(cmd_node->pipe_to_file == 1) {
            close(1);
            dup(output_pipe_fd);
            close(output_pipe_fd);
        } else if(cmd_node->pipe_to_user == 1) {
            close(1);
            close(2);
            dup(output_pipe_fd);
            dup(output_pipe_fd);
            close(output_pipe_fd);
        } else {
            dup2(client->client_sc_fd, 1);
        }
        execvp(cmd_node->cmd, cmd_node->args);
        exit(-1);

    } else if(pipe_count != 0) {
        int status;
        wait(&status);
        // waitpid(pid, &status, 0);
        if(WEXITSTATUS(status) != 0){
            inscrease_all_pipe_node(client->pipe_list);
            return -1;
        } else {
            if(input_pipe_fd != -1) {
                close(input_pipe_fd);
            }
        }
    } else {
        int status;
        wait(&status);
        // waitpid(pid, &status, 0);
        if(WEXITSTATUS(status) != 0){
            inscrease_all_pipe_node(client->pipe_list);
            return -1;
        } else {
            if(in_pipe_node != NULL) {
                close(in_pipe_node->out_fd);
                in_pipe_node->count--;
            }
            if(cmd_node->pipe_to_user == 1) {
                if(fcntl(output_pipe_fd, F_GETFD) != -1) {
                    close(output_pipe_fd);
                }
                char buffer[2048];
                bzero(buffer, 2048);
                read(tmp_global_pipe[0], buffer, 2048);
                //printf("[DBG] Buffer:\n");
                //printf("%s\n", buffer);
                add_global_pipe(client->id, cmd_node->to_user_id, buffer);
                close(tmp_global_pipe[0]);
            }
            close_unused_fd(client);
        }
    }
    if(cmd_node->pipe_from_user == 1) {
        char* msg_tmp = "*** %s (#%d) just received from %s (#%d) by '%s' ***\n";
        char msg[100];
        bzero(msg, 100);
        char from_client_name[30];
        get_client_name(cmd_node->from_user_id, from_client_name);
        sprintf(msg, msg_tmp, client->name, client->id, from_client_name, cmd_node->from_user_id, last_line);
        broad_cast(client, msg);
        remove_global_pipe(cmd_node->from_user_id, client->id, 1);
    }
    return 0;
}

int get_file_fd(char* filename) {
    return open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IROTH);
}

int fd_need_by_other_pipe(client_node_t* client, int fd) {
     pipe_node_t* tmp_list = client->pipe_list;
     while(tmp_list != NULL) {
        if(tmp_list->count > 0) {
            if(tmp_list->in_fd == fd || tmp_list->out_fd == fd) {
                return 1;
            }
        }
        tmp_list = tmp_list->next_node;
     }
     return 0;
}

void close_unused_fd(client_node_t* client) {
    pipe_node_t* tmp_list = client->pipe_list;
    while(tmp_list != NULL) {

        if(tmp_list->count <= 0) {
            if(fcntl(tmp_list->out_fd, F_GETFD) != -1) {
                if(fd_need_by_other_pipe(client, tmp_list->out_fd) != 1){
                    close(tmp_list->out_fd);
                }
            }
            if(fcntl(tmp_list->in_fd, F_GETFD) != -1) {
                if(fd_need_by_other_pipe(client, tmp_list->in_fd) != 1) {
                    close(tmp_list->in_fd);
                }
            }
        }
        tmp_list = tmp_list->next_node;
    }

    pipe_node_t* new_list = NULL;
    tmp_list = client->pipe_list;
    while(tmp_list != NULL) {
        if(tmp_list->count > 0) {
            pipe_node_t* tmp_node = tmp_list;
            tmp_list = tmp_list->next_node;
            tmp_node->next_node = NULL;
            insert_pipe_node(&new_list, tmp_node);
        } else {
            tmp_list = tmp_list->next_node;
        }
    }
    client->pipe_list = new_list;
}

void handle_from_user_node(cmd_node_t** cmd_list) {
    // assume there's only one 'from user node'
    cmd_node_t* tmp_list = *cmd_list;

    while(tmp_list->next_node != NULL) {
        if(tmp_list->next_node->pipe_from_user == 1) {
            cmd_node_t* tmp_node = tmp_list->next_node;
            tmp_list->next_node = tmp_node->next_node;
            tmp_node->next_node = *cmd_list;
            *cmd_list = tmp_node;
            break;
        }
        tmp_list = tmp_list->next_node;
    }

}
