#include "server.h"
#include <errno.h>

void auth_init()
{
    users_fd=open("data/users.bin",O_RDWR);
    if(users_fd==-1)
    {
        log_message("INFO","Users data file not found, creating new one.");
        users_fd=open("data/users.bin",O_RDWR|O_CREAT,0644);
        if(users_fd==-1)
        {
            log_message("ERROR","Failed to create users data file: %s",strerror(errno));
            exit(1);
        }

        user_record admin;
        memset(&admin,0,sizeof(admin));
        admin.user_id=0;
        admin.is_active=1;
        strcpy(admin.username,"admin");
        strcpy(admin.role,"ADMIN");
        sha256_hash("admin123",admin.password_hash);

        write(users_fd,&admin,sizeof(admin));
        log_message("INFO","Default admin user created with username 'admin' and password 'admin123'");

    }
    log_message("INFO","Authentication system initialized successfully");

}

int auth_authenticate(const char *username,const char *password ,int *role_out)
{
    user_record rec;
    int found=0;
    lseek(users_fd,0,SEEK_SET);

    while(read(users_fd,&rec,sizeof(rec))==sizeof(rec))
    {
        if(rec.is_active && strcmp(rec.username,username)==0)
        {
            found=1;
            break;
        }
    }
    
    if(!found)
    {
        return 0;
    }
    
    char computed_hash[65];
    sha256_hash(password,computed_hash);
    if(safe_memcmp(computed_hash,rec.password_hash,64)!=0)
    {
        return 0;
    }

    if(strcmp(rec.role,"ADMIN")==0)
    {
        *role_out=ROLE_ADMIN;
    }
    else if(strcmp(rec.role,"MODERATOR")==0)
    {
        *role_out=ROLE_MODERATOR;
    }
    else
    {
        *role_out=ROLE_USER; // default to user if role is unrecognized
    }

    return 1;
}

int auth_check_permission(int role,int command)
{
    switch (command)
    {
    case CMD_LOGIN:
    case CMD_LOGOUT:
    case CMD_JOIN:
    case CMD_LEAVE:
    case CMD_LIST:
    case CMD_STATUS:
        return 1; // all roles can do these
    case CMD_PROMOTE:
        return role==ROLE_ADMIN; // only admin can promote
    case CMD_CREATE:
    case CMD_KICK:
    case CMD_MUTE:
        return role==ROLE_ADMIN || role==ROLE_MODERATOR; // only admin and mod can do these
        
    
    default:
        return 0;;
    }
}

int auth_add_user(const char *username,const char *password,const char *role)
{
    int user_count=0;
    user_record rec;
    lseek(users_fd,0,SEEK_SET);

    while(read(users_fd,&rec,sizeof(rec))==sizeof(rec))
    {
        if(rec.is_active)
        {
            user_count++;
        }
        if(rec.is_active && strcmp(rec.username,username)==0)
        {
            return 0; // username already exists
        }
    }

    user_record new_user;
    memset(&new_user,0,sizeof(new_user));
    new_user.user_id=user_count;
    new_user.is_active=1;
    strcpy(new_user.username,username);
    strcpy(new_user.role,role);
    sha256_hash(password,new_user.password_hash);

    lseek(users_fd,0,SEEK_END);
    write(users_fd,&new_user,sizeof(new_user));

    log_message("INFO","New user added: %s with role %s",username,role);
    return 1;
}

int auth_promote_user(const char *username, const char *new_role)
{
    user_record rec;
    int found=0;
    off_t offset = 0;
    
    lseek(users_fd,0,SEEK_SET);

    while(read(users_fd,&rec,sizeof(rec))==sizeof(rec))
    {
        if(rec.is_active && strcmp(rec.username,username)==0)
        {
            found=1;
            break;
        }
        offset += sizeof(rec);
    }

    if (!found) return 0;

    strncpy(rec.role, new_role, MAX_ROLE_LEN - 1);
    rec.role[MAX_ROLE_LEN - 1] = '\0';
    
    lseek(users_fd, offset, SEEK_SET);
    write(users_fd, &rec, sizeof(rec));

    log_message("INFO", "User %s promoted to %s", username, new_role);
    return 1;
}