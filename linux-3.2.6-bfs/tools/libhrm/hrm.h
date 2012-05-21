#ifndef _HRM_H
#define _HRM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <linux/hrm.h>
#include <config.h>

#ifdef __cplusplus
extern "C" {
#endif

struct hrm {
	int gid;
	bool consumer;

	struct hrm_counter *counter;
	struct hrm_measures *measures;
	struct hrm_goal *goal;
};

typedef struct hrm hrm_t;

int hrm_attach(hrm_t *monitor, int gid, bool consumer);
int hrm_detach(hrm_t *monitor);

int hrm_add_window(hrm_t *monitor, size_t window_size);
int hrm_del_window(hrm_t *monitor, size_t window_size);

int
hrm_set_goal(hrm_t *monitor, size_t window_size, double min_heart_rate,
							double max_heart_rate);
int hrm_unset_goal(hrm_t *monitor);

int hrm_get_windows_number(hrm_t *monitor);
size_t hrm_get_window_size(hrm_t *monitor, int key);

double hrm_seek_heart_rate(const hrm_t *monitor, size_t window_size, int *key);
double hrm_get_heart_rate(const hrm_t *monitor, size_t *window_size, int key);

double
hrm_get_min_heart_rate(const hrm_t *monitor, size_t *window_size);
double
hrm_get_max_heart_rate(const hrm_t *monitor, size_t *window_size);

int hrm_get_tids(const hrm_t *monitor, pid_t tids[], int n);

int heartbeat(hrm_t *monitor, uint64_t n);

#ifdef __cplusplus
}
#endif

#endif /* _HRM_H */

