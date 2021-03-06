#ifndef __CLIENT_NODE_H__
#define __CLIENT_NODE_H__
#include "cmd_node.h"
#include "pipe_node.h"

#define CLI_SHM_KEY 8888

struct client_node {
    int id;
    int client_sc_fd;
    char name[20];
    char ip[16];
    int port;

    cmd_node_t* cmd_exec_list;
    pipe_node_t* pipe_list;

    char env[256][1024];
    char env_val[256][1024];
    char num_env;
    int pid;


    // message box
    int mbox_head;
    int mbox_tail;
};

typedef struct client_node client_node_t;

int client_semid;
client_node_t* client_list;

client_node_t* create_client_node(int client_socket_fd, char ip[16], int port);
int insert_to_client_list(client_node_t* new_client_node);
client_node_t* find_client_node(int fd);
void broad_cast(client_node_t* current_client, char* message);
int tell(client_node_t* current_client, char* client_id_str, char* message);
void set_env_to_client_node(client_node_t* client, char* name, char* val);
void remove_client_node(client_node_t* client);
int who(client_node_t* current_client);
int check_name_exist(char* name);
int is_client_available(int client_id);
void change_client_name(int client_id, char* name);
void get_client_name(int client_id, char* name);
void get_mbox_info(int client_id, int* head, int* tail);
void set_mbox_info(int client_id, int head, int tail);
void set_client_name(int client_id, char* name);

void client_wait();
void client_signal();
#endif


