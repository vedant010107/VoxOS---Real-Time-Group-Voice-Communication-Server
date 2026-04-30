#include "server.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <errno.h>


ring_buffer *shm_ring=NULL;
static sem_t ring_empty;
static sem_t ring_full;
static pthread_mutex_t ring_mutex = PTHREAD_MUTEX_INITIALIZER;


static int fifo_fd=-1;
static pthread_t fifo_thread;

static void *fifo_thread_main(void *arg);


void ipc_manager_init()
{
    int shm_fd=shm_open("/voxos_ring",O_RDWR|O_CREAT,0644);
    if(shm_fd==-1)
    {
        log_message("ERROR","Failed to create shared memory: %s",strerror(errno));
        exit(1);
    }
    if(ftruncate(shm_fd,sizeof(ring_buffer))==-1)
    {
        log_message("ERROR","Failed to set size of shared memory: %s",strerror(errno));
        close(shm_fd);
        exit(1);
    }

    shm_ring=mmap(NULL,sizeof(ring_buffer),PROT_READ|PROT_WRITE,MAP_SHARED,shm_fd,0);
    if(shm_ring==MAP_FAILED)
    {
        log_message("ERROR","Failed to map shared memory: %s",strerror(errno));
        close(shm_fd);
        exit(1);
    }

    memset( shm_ring,0,sizeof(ring_buffer));
    

    sem_init(&ring_empty,1,RING_BUFFER_CAPACITY);
    sem_init(&ring_full,1,0);

    unlink("/tmp/voxos_emergency");

    if(mkfifo("/tmp/voxos_emergency",0600)==-1)
    {
        log_message("ERROR","Failed to create FIFO: %s",strerror(errno));
    }
    else
    {
        fifo_fd=open("/tmp/voxos_emergency",O_RDONLY|O_NONBLOCK);
        if(fifo_fd==-1)
        {
            log_message("ERROR","Failed to open FIFO: %s",strerror(errno));
        }
        else
        {
            pthread_create(&fifo_thread,NULL,fifo_thread_main,NULL);
            log_message("INFO","Emergency FIFO ready: /temp/voxos_emergency");
        }
    }

    log_message("INFO","IPC manager initialized successfully(ring + semaphores)");


}

int ring_buffer_push(ring_buffer *ring, audio_packet *pkt)
{
    sem_wait(&ring_empty);
    pthread_mutex_lock(&ring_mutex);
    // Write at tail, then advance tail
    memcpy(&ring->packets[ring->tail], pkt, sizeof(audio_packet));
    ring->tail = (ring->tail + 1) % RING_BUFFER_CAPACITY;
    ring->count++;
    pthread_mutex_unlock(&ring_mutex);
    sem_post(&ring_full);
    return 1;
}

int ring_buffer_pop(ring_buffer *ring, audio_packet *pkt)
{
    if (sem_trywait(&ring_full) != 0) {
        return 0;
    }
    pthread_mutex_lock(&ring_mutex);
    // Read from head, then advance head
    memcpy(pkt, &ring->packets[ring->head], sizeof(audio_packet));
    ring->head = (ring->head + 1) % RING_BUFFER_CAPACITY;
    ring->count--;
    pthread_mutex_unlock(&ring_mutex);
    sem_post(&ring_empty);
    return 1;
}

static void *fifo_thread_main(void *arg)
{
    (void)arg;
    char buf[256];
    while(server_on)
    {
        ssize_t n=read(fifo_fd,buf,sizeof(buf)-1);
        if(n>0)
        {
            buf[n]='\0';

            char *p=buf+n-1;
            while(p>=buf && (*p=='\n' || *p=='\r' || *p==' '))
            {
                *p='\0';
                p--;
            }
            if(strcmp(buf,"SHUTDOWN")==0)
            {
                log_message("EMERGENCY","Initiating emergency shutdown...");
                server_on=0;
                break;
            }
            else
            {
                log_message("EMERGENCY","Unknown emergency command received: %s",buf);
            }
            
        }
        else if(n==0)
        {
            close(fifo_fd);    
            fifo_fd=open("/tmp/voxos_emergency",O_RDONLY|O_NONBLOCK);
        }
        else
        {
            usleep(100000);
        }
    }
    return NULL;
}

void ipc_manager_cleanup()
{
    if(shm_ring!=NULL)
    {
        munmap(shm_ring,sizeof(ring_buffer));
        shm_unlink("/voxos_ring");
    }

    sem_destroy(&ring_empty);
    sem_destroy(&ring_full);

    if(fifo_fd!=-1)
    {
        close(fifo_fd);
    }
    unlink("/tmp/voxos_emergency");
    log_message("INFO","IPC manager cleaned up successfully");
}