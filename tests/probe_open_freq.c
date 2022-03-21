#include<unistd.h>
#include<stdlib.h>
#include<stdio.h>
#include<time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define TEST_SIZE 1000
#define MEAN_SIZE 50

enum { NS_PER_SECOND = 1000000000 };

void sub_timespec(struct timespec t1, struct timespec t2, struct timespec *td)
{
    td->tv_nsec = t2.tv_nsec - t1.tv_nsec;
    td->tv_sec  = t2.tv_sec - t1.tv_sec;
    if (td->tv_sec > 0 && td->tv_nsec < 0)
    {
        td->tv_nsec += NS_PER_SECOND;
        td->tv_sec--;
    }
    else if (td->tv_sec < 0 && td->tv_nsec > 0)
    {
        td->tv_nsec -= NS_PER_SECOND;
        td->tv_sec++;
    }
}

void add_timespec(struct timespec t1, struct timespec t2, struct timespec *td)
{
    td->tv_nsec = t2.tv_nsec + t1.tv_nsec;
    td->tv_sec  = t2.tv_sec + t1.tv_sec;
    if (td->tv_nsec > NS_PER_SECOND)
    {
        td->tv_nsec -= NS_PER_SECOND;
        td->tv_sec++;
    }
}

// argv[1] is open frequency
int main(int argc, char **argv) {

    if (argc != 2) {
        printf("usage: ./probe_open <open_per_sec>\n");
        return -1;
    }

    int open_per_sec = atoi(argv[1]);

    struct timespec start, finish, delta, mean;
    const struct timespec sleep_time = {
        .tv_nsec = NS_PER_SECOND / open_per_sec,
    };

    for (int j = 0; j < MEAN_SIZE; j++) {
        clock_gettime(CLOCK_REALTIME, &start);

        for (int i = 0; i < TEST_SIZE; i++) {
            int fd = open("/home", O_RDONLY);
            if (fd < 0) {
                printf("Test failed: %d\n", fd);
                return -1;
            }
            close(fd);
            nanosleep(&sleep_time, NULL);
        }
    
        clock_gettime(CLOCK_REALTIME, &finish);
        sub_timespec(start, finish, &delta);
        add_timespec(mean, delta, &mean);

        //printf("[%d open per second] %d.%.9ld\n", open_per_sec, (int)delta.tv_sec, delta.tv_nsec);
    
    }

    float final = (mean.tv_sec*NS_PER_SECOND + mean.tv_nsec) / MEAN_SIZE;

    printf("[%d open per second] %f\n", open_per_sec, final / NS_PER_SECOND);
}