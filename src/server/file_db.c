#include "server.h"

#include <sys/stat.h>
#include <errno.h>



static void wal_restore_old(int record_id, char **lines, int start, int line_count);

void file_db_init()
{
    rooms_fd=open("data/rooms.bin",O_RDWR);
    if(rooms_fd==-1)
    {
        rooms_fd=open("data/rooms.bin",O_RDWR|O_CREAT,0644);
        if(rooms_fd==-1)
        {
            log_message("ERROR","Failed to create rooms data file: %s",strerror(errno));
            exit(1);
        }
        log_message("INFO","Rooms data file created successfully");
    }

    wal_fd=open("data/wal.log",O_RDWR);
    if(wal_fd==-1)
    {
        wal_fd=open("data/wal.log",O_RDWR|O_CREAT,0644);
        if(wal_fd==-1)
        {
            log_message("ERROR","Failed to create WAL data file: %s",strerror(errno));
            exit(1);
        }
        log_message("INFO","WAL data file created successfully");
    }

    wal_recover();
    log_message("INFO","File-based database ready");


}

int db_read_room(int room_id,room_record *rec)
{
    if(rooms_fd==-1)
    {
        return 0;
    }

    off_t offset=room_id*sizeof(room_record);

    lseek(rooms_fd,offset,SEEK_SET);
    ssize_t bytes=read(rooms_fd,rec,sizeof(room_record));

    if(bytes!=sizeof(room_record))
    {
        return 0;
    }

    if(!rec->is_active)
    {
        return 0;
    }

    return 1;
}

int db_write_room(int room_id,room_record *rec)
{
    if(rooms_fd==-1)
    {
        return 0;
    }
    
    room_record old_rec;
    int has=db_read_room_raw(room_id,&old_rec);

    wal_begin(room_id,has? &old_rec : NULL,rec);

    struct flock lock;
    memset(&lock,0,sizeof(lock));
    lock.l_type=F_WRLCK;
    lock.l_whence=SEEK_SET;
    lock.l_start=room_id*sizeof(room_record);
    lock.l_len=sizeof(room_record);
    fcntl(rooms_fd,F_SETLKW,&lock);

    off_t offset=room_id*sizeof(room_record);
    lseek(rooms_fd,offset,SEEK_SET);
    ssize_t bytes=write(rooms_fd,rec,sizeof(room_record));


    lock.l_type=F_UNLCK;
    fcntl(rooms_fd,F_SETLK,&lock);

    if(bytes!=sizeof(room_record))
    {
        wal_rollback(room_id);
        return 0;
    }

    wal_commit(room_id);
    return 1;
}

int db_read_room_raw(int room_id,room_record *rec)
{
    if(rooms_fd==-1)
    {
        return 0;
    }

    off_t offset=room_id*sizeof(room_record);

    lseek(rooms_fd,offset,SEEK_SET);
    ssize_t bytes=read(rooms_fd,rec,sizeof(room_record));

    if(bytes!=sizeof(room_record))
    {
        return 0;
    }

    return 1;
}

int db_next_free_room_id()
{
    room_record rec;
    int id=0;
    lseek(rooms_fd,0,SEEK_SET);
    while(read(rooms_fd,&rec,sizeof(room_record))==sizeof(room_record))
    {
        if(!rec.is_active)
        {
            return id;
        }
        id++;
    }
    return id;
}

int db_read_user_by_name(const char *username,user_record *rec)
{
    if(users_fd==-1)
    {
        return 0;
    }

    lseek(users_fd,0,SEEK_SET);
    while(read(users_fd,rec,sizeof(user_record))==sizeof(user_record))
    {
        if(rec->is_active && strcmp(rec->username,username)==0)
        {
            return 1;
        }
    }
    return 0;
}

void wal_begin(int record_id,room_record *old_rec,room_record *new_rec)
{
    char line[512];
    int len;

    len=snprintf(line,sizeof(line),"BEGIN %d\n",record_id);

    write(wal_fd,line,len);

    if(old_rec!=NULL)
    {
        len=snprintf(line,sizeof(line),"OLD %d %s %s %d %d\n",old_rec->room_id,old_rec->room_name,old_rec->password,old_rec->is_active,old_rec->user_count);
        write(wal_fd,line,len);
    }
    else
    {
        len=snprintf(line,sizeof(line),"OLD NULL\n");
        write(wal_fd,line,len);
    }

    len=snprintf(line,sizeof(line),"NEW %d %s %s %d %d\n",new_rec->room_id,new_rec->room_name,new_rec->password,new_rec->is_active,new_rec->user_count);

    write(wal_fd,line,len);

    fsync(wal_fd);
}

void wal_commit(int record_id)
{
    char line[512];
    int len=snprintf(line,sizeof(line),"COMMIT %d\n",record_id);
    write(wal_fd,line,len);
    fsync(wal_fd);
}

void wal_rollback(int record_id)
{
    char line[512];
    int len = snprintf(line, sizeof(line), "ROLLBACK %d\n", record_id);
    write(wal_fd, line, len);
    fsync(wal_fd);

    log_message("WARN", "WAL: Rolling back transaction %d", record_id);

    
    lseek(wal_fd, 0, SEEK_SET);

    off_t file_size = lseek(wal_fd, 0, SEEK_END);
    if (file_size == 0) return;

    lseek(wal_fd, 0, SEEK_SET);

    char *buffer = malloc(file_size + 1);
    if (buffer == NULL) return;

    ssize_t bytes = read(wal_fd, buffer, file_size);
    if (bytes <= 0) {
        free(buffer);
        return;
    }
    buffer[bytes] = '\0';

    // parse into lines
    char *lines[512];
    int line_count = 0;

    char *tok = strtok(buffer, "\n");
    while (tok != NULL && line_count < 512) {
        lines[line_count++] = tok;
        tok = strtok(NULL, "\n");
    }

    // find the transaction for this record_id
    for (int i = 0; i < line_count; i++) {
        if (strncmp(lines[i], "BEGIN", 5) == 0) {
            int rid;
            sscanf(lines[i], "BEGIN %d", &rid);

            if (rid == record_id) {
                wal_restore_old(record_id, lines, i, line_count);
                break;
            }
        }
    }

    free(buffer);
}

void wal_recover()
{
    if (wal_fd == -1) return;

    lseek(wal_fd, 0, SEEK_SET);

    // get file size
    off_t file_size = lseek(wal_fd, 0, SEEK_END);
    if (file_size == 0) return;

    lseek(wal_fd, 0, SEEK_SET);

    char *buffer = malloc(file_size + 1);
    if (buffer == NULL) return;

    ssize_t bytes = read(wal_fd, buffer, file_size);
    if (bytes <= 0) {
        free(buffer);
        return;
    }
    buffer[bytes] = '\0';

    log_message("INFO", "WAL recovery: scanning for incomplete transactions...");
    log_message("INFO", "WAL content:\n%s", buffer);

    // parse into lines
    char *lines[512];
    int line_count = 0;

    char *tok = strtok(buffer, "\n");
    while (tok != NULL && line_count < 512) {
        lines[line_count++] = tok;
        tok = strtok(NULL, "\n");
    }

    int recovered = 0;
    int rolled_back = 0;

    for (int i = 0; i < line_count; i++) {
        if (strncmp(lines[i], "BEGIN", 5) == 0) {
            int record_id;
            sscanf(lines[i], "BEGIN %d", &record_id);

            int completed = 0;

            // search forward for matching COMMIT or ROLLBACK
            for (int j = i + 1; j < line_count; j++) {
                int check_id;

                // stop at next BEGIN
                if (strncmp(lines[j], "BEGIN", 5) == 0) break;

                if (strncmp(lines[j], "COMMIT", 6) == 0) {
                    sscanf(lines[j], "COMMIT %d", &check_id);
                    if (check_id == record_id) {
                        completed = 1;
                        break;
                    }
                }

                if (strncmp(lines[j], "ROLLBACK", 8) == 0) {
                    sscanf(lines[j], "ROLLBACK %d", &check_id);
                    if (check_id == record_id) {
                        completed = 1;  // already rolled back
                        break;
                    }
                }
            }

            if (completed) {
                recovered++;
                log_message("INFO", "WAL: Transaction %d completed", record_id);
            } else {
                // incomplete — restore OLD data
                log_message("WARN", "WAL: Rolling back incomplete transaction %d", record_id);
                wal_restore_old(record_id, lines, i, line_count);
                rolled_back++;
            }
        }
    }

    log_message("INFO", "WAL recovery: %d completed, %d rolled back", recovered, rolled_back);

    free(buffer);

    // clear WAL after recovery
    ftruncate(wal_fd, 0);
    lseek(wal_fd, 0, SEEK_SET);
}

static void wal_restore_old(int record_id, char **lines, int start, int line_count)
{
    for (int i = start; i < line_count; i++) {
        // stop at next BEGIN
        if (i != start && strncmp(lines[i], "BEGIN", 5) == 0) break;

        if (strncmp(lines[i], "OLD", 3) == 0) {
            // check if this was a new record
            if (strstr(lines[i], "OLD NULL") != NULL) {
                // delete the partial new record
                room_record empty;
                memset(&empty, 0, sizeof(empty));
                empty.is_active = 0;

                struct flock lock;
                memset(&lock, 0, sizeof(lock));
                lock.l_type   = F_WRLCK;
                lock.l_whence = SEEK_SET;
                lock.l_start  = (off_t)record_id * sizeof(room_record);
                lock.l_len    = sizeof(room_record);
                fcntl(rooms_fd, F_SETLKW, &lock);

                lseek(rooms_fd, lock.l_start, SEEK_SET);
                write(rooms_fd, &empty, sizeof(empty));

                lock.l_type = F_UNLCK;
                fcntl(rooms_fd, F_SETLK, &lock);

                log_message("INFO", "WAL: Deleted incomplete new record %d", record_id);
                return;
            }

            // parse old data
            room_record old_rec;
            memset(&old_rec, 0, sizeof(old_rec));
            sscanf(lines[i], "OLD %d %s %s %d %d",
                   &old_rec.room_id, old_rec.room_name,
                   old_rec.password, &old_rec.is_active,
                   &old_rec.user_count);

            // write old data back with lock
            struct flock lock;
            memset(&lock, 0, sizeof(lock));
            lock.l_type   = F_WRLCK;
            lock.l_whence = SEEK_SET;
            lock.l_start  = (off_t)record_id * sizeof(room_record);
            lock.l_len    = sizeof(room_record);
            fcntl(rooms_fd, F_SETLKW, &lock);

            lseek(rooms_fd, lock.l_start, SEEK_SET);
            write(rooms_fd, &old_rec, sizeof(old_rec));

            lock.l_type = F_UNLCK;
            fcntl(rooms_fd, F_SETLK, &lock);

            log_message("INFO", "WAL: Restored old data for record %d", record_id);
            return;
        }
    }
}

