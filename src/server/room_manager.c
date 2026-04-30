#include "server.h"

void room_manager_init()
{
    room_head=NULL;
    room_count=0;
    log_message("INFO","Room manager initialized");
}

int room_create(const char * name,const char *password,client *creator)
{
    if(room_find_by_name(name)!=NULL)
    {
        return -1; // room with this name already exists
    }

    if(room_count>=MAX_ROOMS)
    {
        return -2; // max room limit reached
    }

    int room_id=db_next_free_room_id();
    room *new_room=malloc(sizeof(room));

    if(new_room==NULL)
    {
        return -3; // memory allocation failed
    }

    memset(new_room,0,sizeof(room));
    new_room->room_id=room_id;
    strcpy(new_room->room_name,name);
    strcpy(new_room->password,password);
    new_room->participant_count=0;
    new_room->active_speakers=0;
    new_room->prev=NULL;
    new_room->next=NULL;
    pthread_mutex_init(&new_room->lock,NULL);

    room_record rec;
    rec.room_id=room_id;
    rec.is_active=1;
    rec.user_count=0;
    strcpy(rec.room_name,name);
    strcpy(rec.password,password);

    if(!db_write_room(room_id,&rec))
    {
        pthread_mutex_destroy(&new_room->lock);
        free(new_room);
        return -4; // failed to write to database
    }   

    pthread_mutex_lock(&rooms_mutex);

    if(room_head==NULL)
    {
        room_head=new_room;
    }
    else
    {
        room *current=room_head;
        while(current->next!=NULL)
        {
            current=current->next;
        }
        current->next=new_room;
        new_room->prev=current;
    }
    room_count++;

    pthread_mutex_unlock(&rooms_mutex);

    log_message("INFO","Room created: %s (ID: %d) by user %s",name,room_id,creator->username);
    return room_id;
}

int room_delete(int room_id)
{
    pthread_mutex_lock(&rooms_mutex);
    room *r=room_head;
    while(r!=NULL && r->room_id!=room_id)
    {
        r=r->next;
    }   

    if(r==NULL)
    {
        pthread_mutex_unlock(&rooms_mutex);
        return 0; // room not found
    }

    if(r->prev!=NULL)
    {
        r->prev->next=r->next;
    }
    else
    {
        room_head=r->next; // deleting head
    }

    if(r->next!=NULL)
    {
        r->next->prev=r->prev;
    }

    room_count--;
    pthread_mutex_unlock(&rooms_mutex);

    room_record rec;
    if(db_read_room(room_id,&rec))
    {
        rec.is_active=0;
        db_write_room(room_id,&rec);
    }

    pthread_mutex_destroy(&r->lock);
    free(r);

    log_message("INFO","Room deleted: ID %d",room_id);
    return 1;

}

int room_join(int room_id,const char* password,client *c)
{
    if(c==NULL)
    {
        return -1; // invalid client
    }

    if(c->current_room!=NULL)
    {
        room_leave(c); // already in a room, must leave first
    }

    pthread_mutex_lock(&rooms_mutex);

    room *r=room_head;
    while(r!=NULL && r->room_id!=room_id)
    {
        r=r->next;
    }
    
    if(r==NULL)
    {
        pthread_mutex_unlock(&rooms_mutex);
        return -2; // room not found
    }

    pthread_mutex_unlock(&rooms_mutex);

    pthread_mutex_lock(&r->lock);

    if(strlen(r->password)>0)
    {
        if(strcmp(r->password,password)!=0)
        {
            pthread_mutex_unlock(&r->lock);
            return -3; // incorrect password
        }
    }

    if(r->participant_count>=MAX_CLIENTS)
    {
        pthread_mutex_unlock(&r->lock);
        return -4; // room full
    }

    r->participants[r->participant_count]=c;
    r->participant_count++;
    c->current_room=r;

    pthread_mutex_unlock(&r->lock);

    room_record rec;
    if(db_read_room_raw(room_id,&rec))
    {
        rec.user_count=r->participant_count;
        db_write_room(room_id,&rec);
    }

    log_message("INFO","Client %s joined room %s (ID: %d)",c->username,r->room_name,room_id);
    return 0;
}

int room_leave(client *c)
{
    if(c==NULL  || c->current_room==NULL)
    {
        return -1; // not in a room
    }

    room *r=c->current_room;
    pthread_mutex_lock(&r->lock);

    // int found=0;
    for(int i=0;i<r->participant_count;i++)
    {
        if(r->participants[i]==c)
        {
            // found=1;
            for(int j=i;j<r->participant_count-1;j++)

            {
                r->participants[j]=r->participants[j+1];
            }
            r->participants[r->participant_count-1]=NULL;
            r->participant_count--;
            if(!c->is_mute)
            {
                r->active_speakers--;
            }
            c->current_room=NULL;
            break;
        }
    }

    pthread_mutex_unlock(&r->lock);

    room_record rec;
    if(db_read_room_raw(r->room_id,&rec))
    {
        rec.user_count=r->participant_count;
        db_write_room(r->room_id,&rec);
    }

    log_message("INFO","Client %s left room %s (ID: %d)",c->username,r->room_name,r->room_id);

    if(r->participant_count==0)
    {
        room_delete(r->room_id);
    }

    return 0;
}


room *room_find_by_name(const char *name)
{
    pthread_mutex_lock(&rooms_mutex);
    room *r=room_head;
    while(r!=NULL)
    {
        if(strcmp(r->room_name,name)==0)
        {
            pthread_mutex_unlock(&rooms_mutex);
            return r;
        }
        r=r->next;
    }
    pthread_mutex_unlock(&rooms_mutex);
    return NULL; // not found
}


room *room_find_by_id(int room_id)
{
    pthread_mutex_lock(&rooms_mutex);
    room *r=room_head;
    while(r!=NULL)
    {
        if(r->room_id==room_id)
        {
            pthread_mutex_unlock(&rooms_mutex);
            return r;
        }
        r=r->next;
    }
    pthread_mutex_unlock(&rooms_mutex);
    return NULL; // not found
}

void room_list_formatted(char *buffer,int buffer_size)
{
    pthread_mutex_lock(&rooms_mutex);
    if(room_head==NULL)
    {
        snprintf(buffer,buffer_size,"No active rooms");
        pthread_mutex_unlock(&rooms_mutex);
        return;
    }

    int pos=0;
    room *r=room_head;
    pos+=snprintf(buffer+pos,buffer_size-pos,"%-5s %-20s %-10s %s\n","ID","Name","Users","Protected");
    while(r!=NULL && pos<buffer_size-1)
    {
        pos+=snprintf(buffer+pos,buffer_size-pos,"%-5d %-20s %-10d %s\n",r->room_id,r->room_name,r->participant_count,(strlen(r->password)>0)?"Yes":"No");
        r=r->next;
    }
    pthread_mutex_unlock(&rooms_mutex); 
}

void remove_client_from_room(room *r,client *c)
{
    if(r==NULL || c==NULL)
    {
        return;
    }
    pthread_mutex_lock(&r->lock);

    // int found=0;
    for(int i=0;i<r->participant_count;i++)
    {
        if(r->participants[i]==c)
        {
            // found=1;
            for(int j=i;j<r->participant_count-1;j++)
            {
                r->participants[j]=r->participants[j+1];
            }
            r->participants[r->participant_count-1]=NULL;
            r->participant_count--;
            if(!c->is_mute)
            {
                r->active_speakers--;
            }
            break;
        }
    }

    pthread_mutex_unlock(&r->lock);

    room_record rec;
    if(db_read_room_raw(r->room_id,&rec))
    {
        rec.user_count=r->participant_count;
        db_write_room(r->room_id,&rec);
    }

    if(r->participant_count==0)
    {
        room_delete(r->room_id);
    }
}   