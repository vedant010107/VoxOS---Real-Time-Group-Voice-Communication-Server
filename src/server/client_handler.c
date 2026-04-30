#include "server.h"
#include <errno.h>


#define WORKER_THREADS 4
#define MAX_QUEUED_TASKS 64

typedef struct 
{
    client * c;
    char command[MAX_PACKET_SIZE];
}worker_task;

static worker_task task_queue[MAX_QUEUED_TASKS];
static int task_count=0;
static int task_head=0;
static int task_tail=0;
static pthread_mutex_t task_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t task_cond = PTHREAD_COND_INITIALIZER;
static pthread_t worker_threads[WORKER_THREADS];

static void *worker_thread_main(void *arg);
static void handle_login(client *c, char *args);
static void handle_register(client *c, char *args);
static void handle_logout(client *c);
static void handle_create_room(client *c, char *args);
static void handle_join_room(client *c, char *args);
static void handle_leave_room(client *c);
static void handle_list_rooms(client *c);
static void handle_kick(client *c, char *args);
static void handle_mute(client *c, char *args);
static void handle_status(client *c);
static void handle_promote(client *c, char *args);
static void send_error(client *c, int status, const char *msg);
static void send_ok(client *c, const char *msg, const char *data);

void client_handler_init()
{
    for(int i=0;i<WORKER_THREADS;i++)
    {
        pthread_create(&worker_threads[i],NULL,worker_thread_main,NULL);
    }
    log_message("INFO","Client handler initialized with %d worker threads",WORKER_THREADS);
}

void process_client_command(client *c, const char *command)
{

    pthread_mutex_lock(&task_mutex);
    if(task_count==MAX_QUEUED_TASKS)
    {
        pthread_mutex_unlock(&task_mutex);
        send_error(c,STATUS_ERR_INTERNAL,"Server is too busy. Try again later.");
        pthread_mutex_lock(&clients_mutex);
        c->ref_count--;
        if(c->ref_count == 0) free(c);
        pthread_mutex_unlock(&clients_mutex);
        return;
    }

    worker_task *task=&task_queue[task_tail];
    task->c=c;  
    strcpy(task->command,command);
    task->command[MAX_PACKET_SIZE-1]='\0'; // ensure null termination   

    task_tail=(task_tail+1)%MAX_QUEUED_TASKS;
    task_count++;
    pthread_cond_signal(&task_cond);
    pthread_mutex_unlock(&task_mutex);
}

static void *worker_thread_main(void *arg)
{
    (void) arg; 
    while(server_on)
    {
        pthread_mutex_lock(&task_mutex);
        while(task_count==0 && server_on)
        {
            pthread_cond_wait(&task_cond,&task_mutex);
        }

        if(!server_on)
        {
            pthread_mutex_unlock(&task_mutex);
            break;
        }

        worker_task task=task_queue[task_head];
        task_head=(task_head+1)%MAX_QUEUED_TASKS;
        task_count--;
        pthread_mutex_unlock(&task_mutex);

        if(task.c!=NULL && task.c->is_active)
        {
            char *saveptr;
            char *cmd=strtok_r(task.command," \n\r",&saveptr);
            char *args=strtok_r(NULL,"",&saveptr);

            if(cmd==NULL) continue;

            if(strcasecmp(cmd,"LOGIN")==0)
            {
                handle_login(task.c,args);
            }
            else if(strcasecmp(cmd,"REGISTER")==0)
            {
                handle_register(task.c,args);
            }
            else if(strcasecmp(cmd,"LOGOUT")==0)
            {
                handle_logout(task.c);
            }
            else if(strcasecmp(cmd,"CREATE")==0)
            {
                handle_create_room(task.c,args);
            }
            else if(strcasecmp(cmd,"JOIN")==0)
            {
                handle_join_room(task.c,args);
            }
            else if(strcasecmp(cmd,"LEAVE")==0)
            {
                handle_leave_room(task.c);
            }
            else if(strcasecmp(cmd,"LIST")==0)
            {
                handle_list_rooms(task.c);
            }
            else if(strcasecmp(cmd,"KICK")==0)
            {
                handle_kick(task.c,args);
            }
            else if(strcasecmp(cmd,"MUTE")==0)
            {
                handle_mute(task.c,args);
            }
            else if(strcasecmp(cmd,"STATUS")==0)
            {
                handle_status(task.c);
            }
            else if(strcasecmp(cmd,"PROMOTE")==0)
            {
                handle_promote(task.c,args);
            }
            else
            {
                send_error(task.c,STATUS_ERR_NOT_FOUND,"Unknown command");
            }
        }

        if (task.c != NULL) {
            pthread_mutex_lock(&clients_mutex);
            task.c->ref_count--;
            if(task.c->ref_count == 0) free(task.c);
            pthread_mutex_unlock(&clients_mutex);
        }
    }
    return NULL;
}

static void handle_login(client *c, char *args)
{
    // implementation of login command
    if(c->user_id!=-1)
    {
        send_error(c,STATUS_ERR_CONFLICT,"Already logged in");
        return;
    }

    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];

    if(args!=NULL)
    {
        sscanf(args,"%s %s",username,password);
    }
    
    if(strlen(username)==0 || strlen(password)==0)
    {
        send_error(c,STATUS_ERR_AUTH,"Username and password required");
        return;
    }

    int role;
    if(auth_authenticate(username,password,&role))
    {
        c->user_id=0;
        strncpy(c->username,username,MAX_USERNAME_LEN-1);
        c->username[MAX_USERNAME_LEN-1]='\0';
        c->role=role;
        char slot_str[16];
        sprintf(slot_str, "%d", c->slot);
        send_ok(c,"Login successful",slot_str);
        log_message("INFO","Client fd %d logged in as %s with role %d",c->fd,c->username,c->role);
    }
    else
    {
        send_error(c,STATUS_ERR_AUTH,"Invalid username or password");
    }


}

static void handle_register(client *c, char *args)
{
    if(c->user_id!=-1)
    {
        send_error(c,STATUS_ERR_CONFLICT,"Already logged in");
        return;
    }

    char username[MAX_USERNAME_LEN] = {0};
    char password[MAX_PASSWORD_LEN] = {0};

    if(args!=NULL)
    {
        sscanf(args,"%s %s",username,password);
    }
    
    if(strlen(username)==0 || strlen(password)==0)
    {
        send_error(c,STATUS_ERR_AUTH,"Username and password required");
        return;
    }

    if(auth_add_user(username, password, "USER"))
    {
        send_ok(c,"Registration successful",NULL);
    }
    else
    {
        send_error(c,STATUS_ERR_CONFLICT,"Username already exists");
    }
}

static void handle_logout(client *c)
{
    // implementation of logout command
    if(c->user_id==-1)
    {
        send_error(c,STATUS_ERR_AUTH,"Not logged in");
        return;
    }

    if(c->current_room!=NULL)
    {
        room_leave(c);
    }

    c->user_id=-1;
    c->username[0]='\0';
    c->role=ROLE_GUEST;
    send_ok(c,"Logout successful",NULL);
    log_message("INFO","Client fd %d logged out",c->fd);
}

static void handle_create_room(client *c, char *args)
{
    if(c->user_id==-1)
    {
        send_error(c,STATUS_ERR_AUTH,"Login required");
        return;
    }

    if(!auth_check_permission(c->role, CMD_CREATE))
    {
        send_error(c,STATUS_ERR_FORBIDDEN,"Insufficient permissions to create room");
        return;
    }

    char room_name[MAX_ROOM_NAME_LEN];
    char password[MAX_PASSWORD_LEN];

    if(args!=NULL)
    {
        sscanf(args,"%s %s",room_name,password);
    }

    if(strlen(room_name)==0)
    {
        send_error(c,STATUS_ERR_NOT_FOUND,"Room name required");
        return;
    }

    int result=room_create(room_name,password,c);

    switch(result)
    {
        case -1:
            send_error(c,STATUS_ERR_CONFLICT,"Room with this name already exists");
            break;
        case -2:
            send_error(c,STATUS_ERR_FULL,"Maximum room limit reached");
            break;
        case -3:
            send_error(c,STATUS_ERR_INTERNAL,"Failed to create room due to server error");
            break;
        case -4:
            send_error(c,STATUS_ERR_INTERNAL,"Failed to write room to database");
            break;
        default:
            send_ok(c,"Room created successfully",NULL);
            break;
    }
}

static void handle_join_room(client *c, char *args)
{
    if(c->user_id==-1)
    {
        send_error(c,STATUS_ERR_AUTH,"Login required");
        return;
    }

    char room_name[MAX_ROOM_NAME_LEN];
    char room_password[MAX_PASSWORD_LEN];
    if(args!=NULL)
    {
        sscanf(args,"%s %s",room_name,room_password);
    }

    if(strlen(room_name)==0)
    {
        send_error(c,STATUS_ERR_NOT_FOUND,"Room name required");
        return;
    }

    room *r=room_find_by_name(room_name);
    if(r==NULL)
    {
        send_error(c,STATUS_ERR_NOT_FOUND,"Room not found");
        return;
    }

    int result=room_join(r->room_id,room_password,c);

    switch(result)
    {
        case -1:
            send_error(c,STATUS_ERR_INTERNAL,"Failed to join room due to server error");
            break;
        case -2:
            send_error(c,STATUS_ERR_NOT_FOUND,"Room not found");
            break;
        case -3:
            send_error(c,STATUS_ERR_FORBIDDEN,"Incorrect password");
            break;
        case -4:
            send_error(c,STATUS_ERR_FULL,"Room is full");
            break;
        default:
            send_ok(c,"Joined room successfully",r->room_name);
            break;
    }
}

static void handle_leave_room(client *c)
{
    if(c->user_id==-1)
    {
        send_error(c,STATUS_ERR_AUTH,"Login required");
        return;
    }

    if(c->current_room==NULL)
    {
        send_error(c,STATUS_ERR_NOT_FOUND,"Not currently in a room");
        return;
    }

    room_leave(c);
    send_ok(c,"Left room successfully",NULL);
}

static void handle_list_rooms(client *c)
{
    if(c->user_id==-1)
    {
        send_error(c,STATUS_ERR_AUTH,"Login required");
        return;
    }

    char buffer[MAX_PACKET_SIZE];
    room_list_formatted(buffer,sizeof(buffer));
    send_ok(c,"Active rooms:",buffer);
}

static void handle_kick(client *c, char *args)
{
    if(c->user_id==-1)
    {
        send_error(c,STATUS_ERR_AUTH,"Login required");
        return;
    }

    if(!auth_check_permission(c->role, CMD_KICK))
    {
        send_error(c,STATUS_ERR_FORBIDDEN,"Permission denied");
        return;
    }

    if(c->current_room==NULL)
    {
        send_error(c,STATUS_ERR_NOT_FOUND,"Not currently in a room");
        return;
    }

    char target_name[MAX_USERNAME_LEN];
    if(args!=NULL)
    {
        sscanf(args,"%s",target_name);

    }

    if(strlen(target_name)==0)
    {
        send_error(c,STATUS_ERR_NOT_FOUND,"Target username required");
        return;
    }

    pthread_mutex_lock(&c->current_room->lock);
    client *target=NULL;
    for(int i=0;i<c->current_room->participant_count;i++)
    {
        client *p=c->current_room->participants[i];
        if(p!=NULL && strcmp(p->username,target_name)==0)
        {
            target=p;
            break;
        }
    }

    pthread_mutex_unlock(&c->current_room->lock);

    if(target==NULL)
    {
        send_error(c,STATUS_ERR_NOT_FOUND,"Target user not found in the room");
        return;
    }

    room_leave(target);
    log_message("INFO","Client %s kicked user %s from room %s",c->username,target->username,c->current_room->room_name);
    send_ok(c,"User kicked successfully",NULL);
}

static void handle_mute(client *c,char *args)
{
    if(c->user_id==-1)
    {
        send_error(c,STATUS_ERR_AUTH,"Login required");
        return;
    }

    if(!auth_check_permission(c->role, CMD_MUTE))
    {
        send_error(c,STATUS_ERR_FORBIDDEN,"Permission denied");
        return;
    }

    if(c->current_room==NULL)
    {
        send_error(c,STATUS_ERR_NOT_FOUND,"Not currently in a room");
        return;
    }

    char target_name[MAX_USERNAME_LEN];
    if(args!=NULL)
    {
        sscanf(args,"%s",target_name);
    }

    if(strlen(target_name)==0)
    {
        send_error(c,STATUS_ERR_NOT_FOUND,"Target username required");
        return;
    }

    pthread_mutex_lock(&c->current_room->lock);
    client *target=NULL;
    for(int i=0;i<c->current_room->participant_count;i++)
    {
        client *p=c->current_room->participants[i];
        if(p!=NULL && strcmp(p->username,target_name)==0)
        {
            target=p;
            break;
        }
    }
    if(target!=NULL)
    {
        target->is_mute=!target->is_mute;
        if(target->is_mute)
        {
            c->current_room->active_speakers--;
        }
        else
        {
            c->current_room->active_speakers++;
        }
        log_message("INFO","Client %s %s user %s in room %s",c->username,target->is_mute?"muted":"unmuted",target->username,c->current_room->room_name);
        send_ok(c,target->is_mute?"User muted successfully":"User unmuted successfully",NULL);
    }
    else
    {
        send_error(c,STATUS_ERR_NOT_FOUND,"Target user not found in the room");
        return ;
    }
    pthread_mutex_unlock(&c->current_room->lock);
}

static void handle_status(client *c) 
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer),"Username: %s | Role: %d | Room: %s | Muted: %s", c->username, c->role,
    (c->current_room != NULL) ? c->current_room->room_name : "None",c->is_mute ? "Yes" : "No");
    send_ok(c, buffer, NULL);
}

static void handle_promote(client *c, char *args)
{
    if(c->user_id==-1)
    {
        send_error(c,STATUS_ERR_AUTH,"Login required");
        return;
    }

    if(!auth_check_permission(c->role, CMD_PROMOTE))
    {
        send_error(c,STATUS_ERR_FORBIDDEN,"Permission denied. Only admins can promote.");
        return;
    }

    char target_name[MAX_USERNAME_LEN] = {0};
    char role_name[MAX_ROLE_LEN] = {0};
    
    if(args!=NULL)
    {
        sscanf(args,"%s %s",target_name, role_name);
    }

    if(strlen(target_name)==0 || strlen(role_name)==0)
    {
        send_error(c,STATUS_ERR_NOT_FOUND,"Target username and role required");
        return;
    }

    if (strcasecmp(role_name, "MODERATOR") != 0 && strcasecmp(role_name, "USER") != 0 && strcasecmp(role_name, "ADMIN") != 0) {
        send_error(c, STATUS_ERR_CONFLICT, "Invalid role (must be ADMIN, MODERATOR, or USER)");
        return;
    }

    // Uppercase the role name
    for (int i=0; role_name[i]; i++) {
        if (role_name[i] >= 'a' && role_name[i] <= 'z') role_name[i] -= 32;
    }

    if (auth_promote_user(target_name, role_name)) {
        // Also update in-memory role if the user is currently connected
        pthread_mutex_lock(&clients_mutex);
        for (int i=0; i<MAX_CLIENTS; i++) {
            if (clients[i] != NULL && clients[i]->is_active && strcmp(clients[i]->username, target_name) == 0) {
                if (strcmp(role_name, "ADMIN") == 0) clients[i]->role = ROLE_ADMIN;
                else if (strcmp(role_name, "MODERATOR") == 0) clients[i]->role = ROLE_MODERATOR;
                else clients[i]->role = ROLE_USER;
            }
        }
        pthread_mutex_unlock(&clients_mutex);
        
        send_ok(c, "User promoted successfully", NULL);
    } else {
        send_error(c, STATUS_ERR_NOT_FOUND, "Target user not found");
    }
}

static void send_error(client *c, int status, const char *msg) 
{
    response_packet resp;
    memset(&resp, 0, sizeof(resp));
    resp.opcode=0;
    resp.status=status;
    strncpy(resp.message, msg, 255);
    resp.message[255]='\0';
    send_response(c->fd, &resp);
}

static void send_ok(client *c, const char *msg, const char *data) 
{
    response_packet resp;
    memset(&resp, 0, sizeof(resp));
    resp.opcode= 0;
    resp.status=STATUS_OK;
    strncpy(resp.message, msg, 255);
    resp.message[255]='\0';
    if (data!=NULL) 
    {
        strncpy(resp.data,data,MAX_PACKET_SIZE-1);
        resp.data[MAX_PACKET_SIZE-1]='\0';
    }
    send_response(c->fd,&resp);
}
