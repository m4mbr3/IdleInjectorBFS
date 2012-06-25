#include <getopt.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <sched.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <hrm.h>

#define INC_TO 10000000000
#define NSEC_PER_SEC 1000000000
#define WS {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150}
#define MIN {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}
#define MAX {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}

void printinfo(void)
{
	printf("%d %ld Started \n", getpid(),syscall(SYS_gettid));
}
static inline int64_t timespec_to_ns(const struct timespec *ts)
{
		return ((int64_t) ts->tv_sec * NSEC_PER_SEC) + ts->tv_nsec;
}

pid_t gettid(void)
{
	return syscall(__NR_gettid);
}

void *thread_routine(void *arg)
{
	hrm_t monitor;
	int i;
	int proc_num = (int)(long)arg;
	cpu_set_t set;
	float a = 1;
	float b = 1;
	float c = 1;
	struct timespec t0, t1;
	
	hrm_attach(&monitor, 11, false);

	CPU_ZERO(&set);
	CPU_SET(proc_num, &set);

	if (sched_setaffinity(gettid(), sizeof(cpu_set_t), &set))
	{
		perror("sched_setaffinity");
		return NULL;
	}

	clock_gettime(CLOCK_MONOTONIC, &t0);
	printinfo();
	for (i = 0; i < INC_TO; i++)
	{
		a = b + c;
		c = b / a;
		heartbeat(&monitor, 1);
	}

	clock_gettime(CLOCK_MONOTONIC, &t1);

	printf("Run cpu %d took %lld ns\n", proc_num,
			timespec_to_ns(&t1) - timespec_to_ns(&t0));

	hrm_detach(&monitor);
	return NULL;
}

int main()
{
	int procs = 0;
	int i;
	pthread_t thread[2];
	int windows_number = 3;
	size_t ws[HRM_MAX_WINDOWS] = WS;
	int min[HRM_MAX_WINDOWS] = MIN;
	int max[HRM_MAX_WINDOWS] = MAX;
	hrm_t monitor;
	// Getting number of CPUs
	procs = (int)sysconf(_SC_NPROCESSORS_ONLN);
	if (procs < 0)
	{
		perror("sysconf");
		return -1;
	}
	printinfo();
	for (i = 0; i < procs; i++)
	{
		if (pthread_create(&thread[i], NULL, thread_routine,
			(void *)(long)i))
		{
			perror( "pthread_create" );
			procs = i;
			break;
		}
		//pthread_join(thread[i], NULL);
	}
	for (i = 0; i < windows_number; i++)
		hrm_set_goal(&monitor, ws[i], min[i], max[i]);

	windows_number = hrm_get_windows_number(&monitor);

	for (i = 0; i < procs; i++)
	{
		pthread_join(thread[i],NULL);
	}
	
	return 0;
}

