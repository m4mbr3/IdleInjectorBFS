#include <getopt.h>
#include <pthread.h>
#include <time.h>
#include <sched.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <linux/unistd.h>
#include <hrm.h>

#define NSEC_PER_SEC 1000000000

static inline int64_t timespec_to_ns(const struct timespec *ts)
{
	return ((int64_t) ts->tv_sec * NSEC_PER_SEC) + ts->tv_nsec;
}


int main(int argc, char *argv[])
{
	int opt;
	int gid = 1;
	int period = 1000;
	struct timespec t1,t2;
	hrm_t monitor;
	char *t;
	size_t ws[HRM_MAX_WINDOWS];
	int wn = 0;
	pid_t tids[64];
	int debug = 1;
	FILE *fp;
	while ((opt = getopt(argc, argv, "g:p:w:")) != -1) {
		switch (opt) {
		case 'g':
			gid = strtol(optarg, NULL, 10);
			break;
		case 'p':
			period = strtol(optarg, NULL, 10);
			break;
		case 'w':
			t = strtok(optarg, ",");
			for (wn = 0; wn < HRM_MAX_WINDOWS && t; wn++) {
				ws[wn] = strtol(t, NULL, 10);
				t = strtok(NULL, ",");
			}
			break;
		default:
			return -1;
		}
	}

	hrm_attach(&monitor, gid, true);
	for (int i = 0; i < wn; i++)
		hrm_add_window(&monitor, ws[i]);
	clock_gettime(CLOCK_MONOTONIC, &t2);
	int level = -1;
	while (1) {
		hrm_get_tids(&monitor, tids, 64);
	/*	printf("group %d tids:", gid);
		for (int i = 0; tids[i]; i++)
			printf(" %d", (int) tids[i]);
		printf("\n");
		printf("min heart rate: %f\n", hrm_get_min_heart_rate(&monitor, ws));
		printf("max heart rate: %f\n", hrm_get_max_heart_rate(&monitor, ws));
		printf("goal scope: %zu\n", *ws);
		printf("\tglobal heart rate: %f\n", hrm_get_heart_rate(&monitor, ws, 0));

		printf("%d MA are available:\n",
					hrm_get_windows_number(&monitor));

	
		for (int i = 1; i <= HRM_MAX_WINDOWS; i++) {
			double hr = hrm_get_heart_rate(&monitor, ws, i);
			if (*ws != HRM_MAX_WINDOW_SIZE) {
				printf( "\twindow %d:\n"
					"\t\twindow heart rate: %f\n", i, hr);
				printf("\t\tsize : %zu [timer periods]\n", *ws);
			}
		}
		printf("\n");
	*/	
		if ((fp=fopen("consumer.log", "a+"))==NULL)
			debug = 0;
		double time = (double)(timespec_to_ns(&t1) - timespec_to_ns(&t2))/1000000000;
		FILE * flog;
		if( debug == 1){
			fprintf(fp,"\n-----------------------------------------------------------------------------\n\n");
			clock_gettime(CLOCK_MONOTONIC, &t1);
			fprintf(fp,"time %lf s\n",time);
			fprintf(fp,"group %d tids:", gid);
			for ( int i = 0; tids[i]; i++)
				fprintf(fp," %d", (int) tids[i]);
			fprintf(fp,"\n");
			fprintf(fp, "min heart rate: %f\n", hrm_get_min_heart_rate(&monitor, ws));
			fprintf(fp, "max heart rate: %f\n", hrm_get_max_heart_rate(&monitor, ws));
			fprintf(fp, "goal scope:%zu\n", *ws);
			fprintf(fp, "\tglobal heart rate: %f\n", hrm_get_heart_rate(&monitor,ws, 0));
			fprintf(fp, "%d MA are available:\n", 
							hrm_get_windows_number(&monitor));
			
			for (int i = 1; i <= HRM_MAX_WINDOWS; i++) {
				double hr = hrm_get_heart_rate(&monitor, ws, i);
			if (time > 100 ){
				if( hr  > 10000000 ){
					if (level != 10){
						system ("echo 5 > /proc/schedidle/sched_global");
						level = 10;
						printf ("\nlevel : %d\n" , level);
					}
				}
				else if ( hr > 7000000 ){
					if (level != 7){
						system ("echo 6 > /proc/schedidle/sched_global");
						level = 7;	printf ("\nlevel : %d\n" , level);
					}
				}
				else if ( hr > 4000000 ){
					if (level != 4){
						system ("echo 7 > /proc/schedidle/sched_global");
						level = 4;	printf ("\nlevel : %d\n" , level);
					}		
				}
				else if ( hr > 1000000 ){
					if (level != 1){
						system ("echo 8 > /proc/schedidle/sched_global");
						level = 1;	printf ("\nlevel : %d\n" , level);
					}
				}
			}
				if (*ws != HRM_MAX_WINDOW_SIZE) {
				    if(gid == 1){
					if ( i == 1 ){
						if((flog=fopen("w1.log", "a+")) == NULL) return -1;
						fprintf(flog, "\t %lf: "
							"\t\twindow heart rate: %f\n", time, hr);
						fclose(flog);
					}
					else if ( i == 2 ){
						if((flog=fopen("w2.log", "a+")) == NULL) return -1;
						fprintf(flog, "\t %lf: "
							"\t\twindow heart rate: %f\n", time, hr);
						fclose(flog);
					}
					else if ( i == 3 ) {
						if((flog=fopen("w3.log", "a+")) == NULL) return -1;
						fprintf(flog, "\t %lf: "
							"\t\twindow heart rate: %f\n", time, hr);
						fclose(flog);
					}
					else if ( i == 4 ){
						if((flog=fopen("w4.log", "a+")) == NULL) return -1;
						fprintf(flog, "\t %lf: "
							"\t\twindow heart rate: %f\n", time, hr);
						fclose(flog);
					}
				}else if(gid == 2)
				{
					if ( i == 1 ){
						if((flog=fopen("w1_2.log", "a+")) == NULL) return -1;
						fprintf(flog, "\t %lf: "
							"\t\twindow heart rate: %f\n", time, hr);
						fclose(flog);
					}
					else if ( i == 2 ){
						if((flog=fopen("w2_2.log", "a+")) == NULL) return -1;
						fprintf(flog, "\t %lf: "
							"\t\twindow heart rate: %f\n", time, hr);
						fclose(flog);
					}
					else if ( i == 3 ) {
						if((flog=fopen("w3_2.log", "a+")) == NULL) return -1;
						fprintf(flog, "\t %lf: "
							"\t\twindow heart rate: %f\n", time, hr);
						fclose(flog);
					}
					else if ( i == 4 ){
						if((flog=fopen("w4_2.log", "a+")) == NULL) return -1;
						fprintf(flog, "\t %lf: "
							"\t\twindow heart rate: %f\n", time, hr);
						fclose(flog);
					}

				}
			    }
			}
			fprintf(fp,"\n");
			fclose(fp);
		}	

		debug = 1;
		usleep(period * 1000);
	}

	return 0;
}

