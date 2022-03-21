#include<unistd.h>
#include<stdlib.h>
#include<stdio.h>
#include<time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

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
        printf("usage: ./probe_open <number_of_open>\n");
        return -1;
    }

    int test_size = atoi(argv[1]);

    struct timespec start, finish, delta, mean = {
        .tv_nsec = 0,
        .tv_sec = 0,
    };
    for (int j = 0; j < MEAN_SIZE; j++) {
        clock_gettime(CLOCK_REALTIME, &start);

        for (int i = 0; i < test_size; i++) {
            int fd = open("/dev/sda1", O_RDONLY);
            if (fd < 0) {
                printf("Test failed: %d\n", fd);
                return -1;
            }
            close(fd);
        }
    
        clock_gettime(CLOCK_REALTIME, &finish);
        sub_timespec(start, finish, &delta);
        add_timespec(mean, delta, &mean);

        //printf("[opened %d times] %d.%.9ld\n", test_size, (int)delta.tv_sec, delta.tv_nsec);
    
    }

    float final = (mean.tv_sec*NS_PER_SECOND + mean.tv_nsec) / MEAN_SIZE;

    printf("[opened %d times] %f\n", test_size, final / NS_PER_SECOND);
}
