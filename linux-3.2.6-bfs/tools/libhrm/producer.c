/* Heart Rate Monitor  (HRM)
 *
 *
 * Designed and Implemented by:
 * 
 *      Davide Basilio Bartolini  <bartolini@elet.polimi.it>
 *      Filippo Sironi            <sironi@csail.mit.edu>
 *
 * Modified by:
 * 
 * 	Andrea Mambretti  	  <andrea2.mambretti@mail.polimi.it>
 */
#include <getopt.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include <hrm.h>

#define WS {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150}
#define MIN {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}
#define MAX {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}

int gid = 1;
int threads_number = 1;
long heartbeats_number = 1000000;

int start, running;

void *thread(void *arg);

int
main(int argc, char *argv[])
{
	int opt;
	pthread_t *threads;
	struct timespec tbegin;
	struct timespec tend;
	int windows_number = 0;
	size_t ws[HRM_MAX_WINDOWS] = WS;
	int min[HRM_MAX_WINDOWS] = MIN;
	int max[HRM_MAX_WINDOWS] = MAX;
	hrm_t monitor;
	int i;

	while ((opt = getopt(argc, argv, "g:n:t:w:")) != -1) {
		switch (opt) {
		case 'g':
			gid = strtol(optarg, NULL, 10);
			break;
		case 'n':
			heartbeats_number = strtol(optarg, NULL, 10);
			break;
		case 't':
			threads_number = strtol(optarg, NULL, 10);
			break;
		case 'w':
			windows_number = strtol(optarg, NULL, 10);
			break;
		default:
			return -1;
		}
	}
	if (threads_number < 1 || heartbeats_number < 1) {
		return -1;
	}

	threads = (pthread_t *) malloc(sizeof(pthread_t) * threads_number);
	for (int i = 0; i < threads_number; i++)
		pthread_create(&threads[i], NULL, thread, NULL);
	clock_gettime(CLOCK_MONOTONIC, &tbegin);

	hrm_attach(&monitor, gid, false);
	printf("Attached to gid %d\n", gid);
	for (i = 0; i < windows_number; i++)
		hrm_set_goal(&monitor, ws[i], min[i], max[i]);

	windows_number = hrm_get_windows_number(&monitor);
	printf("\t%d MA set", windows_number);
	if (windows_number) {
		printf(" of sizes: ");
		for (int i = 1; i <= HRM_MAX_WINDOWS; i++) {
			*ws = hrm_get_window_size(&monitor, i);
			if (*ws > 0 && *ws < HRM_MAX_WINDOW_SIZE)
				printf("%zu ", *ws);
		}
		printf("[hrtimer periods]\n");
	} else
		printf(".\n");

	printf("\tmin heart rate: %f\n", hrm_get_min_heart_rate(&monitor, ws));
	printf("\tmax heart rate: %f\n", hrm_get_max_heart_rate(&monitor, ws));
	printf("\tgoal scope: %zu\n", *ws);
	start = 1;

	running = 0;
	while(!running);
	hrm_detach(&monitor);

	for (i = 0; i < threads_number; i++)
		pthread_join(threads[i], NULL);

	hrm_unset_goal(&monitor);
	for (i = 1; i <= HRM_MAX_WINDOWS; i++) {
		hrm_get_heart_rate(&monitor, ws, i);
		if (ws[0] < HRM_MAX_WINDOW_SIZE)
			hrm_del_window(&monitor, ws[0]);
	}

	clock_gettime(CLOCK_MONOTONIC, &tend);
	printf("%lu\n", (unsigned long) ((tend.tv_sec - tbegin.tv_sec) * 1000000000 + (tend.tv_nsec - tbegin.tv_nsec)));

	return 0;
}

void *
thread(void *arg)
{
	hrm_t monitor;
	double a = 10;
	double  b = 40;
	double c = 12;
	while (!start);
	hrm_attach(&monitor, gid, false);
	running = 1;

	for (long i = 0; i < heartbeats_number / threads_number; i++) {
		a = ((a*10)/(b*c))/50;
		b = ((b*b)*c*a*10)/(a*b);
		c = 10 *a *b*c+c;
		a = a/b;
		a = a/b;
		a = c/b;
		b = 10/c;
		heartbeat(&monitor, 1);
		a = 10;
		b = 40;
		c = 12;
	}

	hrm_detach(&monitor);

	pthread_exit(NULL);

	return arg;
}

