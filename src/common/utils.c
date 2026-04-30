#define _POSIX_C_SOURCE 199309L
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <pthread.h>



long long get_time_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long )ts.tv_sec*1000 +ts.tv_nsec/1000000;
}

/*
this is simple hash func 
which uses xor to create a hash 
sha256 is a complex hash func but here we are using simple one for demo purpose only

*/
void sha256_hash(const char *input, char output[65]) {
    unsigned char hash[32];
    memset(hash,0,32);

    for(int i=0;input[i]!='\0';i++)
    {
        hash[i%32] ^= input[i];
        hash[(i+1)%32] += input[i];
    }

    for(int i=0;i<32;i++)
    {
        sprintf(output + i*2, "%02x", hash[i]);
    }
    output[64] = '\0';
}

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_message(const char *level,const char * format,...)
{
    pthread_mutex_lock(&log_mutex);
    time_t now= time(NULL);
    struct tm *t = localtime(&now);
    fprintf(stderr,"[%02d-%02d-%04d %02d:%02d:%02d] [%s] ",t->tm_mday,t->tm_mon+1,t->tm_year+1900,t->tm_hour,t->tm_min,t->tm_sec,level);

    va_list args;
    va_start(args,format);
    vfprintf(stderr,format,args);
    va_end(args);

    fprintf(stderr,"\n");
    fflush(stderr);

    pthread_mutex_unlock(&log_mutex);
}


/*
this is a helper function
where lets say an attacker tries to compare two hashes
if we use memcmp it will return as soon as it finds a mismatch
so the attacker can measure the time taken to compare two hashes
if time take is more so he finds a character match

so he can guess it 

but i am using this function which has to go through all the chars
then only return hence it always gives a constant time for each request
*/

int safe_memcmp(const void *a, const void *b, size_t size) {
    const unsigned char *p1 = (const unsigned char *)a;
    const unsigned char *p2 = (const unsigned char *)b;
    unsigned char result = 0;

    for (size_t i = 0; i < size; i++) {
        result |= p1[i] ^ p2[i];
    }

    return result; // returns 0 if equal,else different
}