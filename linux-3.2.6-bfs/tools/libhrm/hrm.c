/* Heart Rate Monitor  (HRM)
 *
 *
 * Designed and Implemented by:
 * 
 *      Davide Basilio Bartolini  <bartolini@elet.polimi.it>
 *      Filippo Sironi            <sironi@csail.mit.edu>
 *
 *
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include <hrm.h>
#include <linux/hrm.h>

int hrm_attach(hrm_t *monitor, int gid, bool consumer)
{
	int retval = -1;
	int tid;
	long page_size;
	char file[256];
	FILE *group_fp;
	FILE *counter_fp;
	FILE *measures_goal_fp;
	int counter_fd;
	int measures_goal_fd;
	unsigned long counter_address;
	unsigned long measures_address;
	unsigned long goal_address;

	if (!monitor) {
		errno = EFAULT;
		return -1;
	}
	if (gid == 0) {
		errno = EDOM;
		return -1;
	}

	tid = syscall(__NR_gettid);
	page_size = sysconf(_SC_PAGESIZE);

	sprintf(file, "/proc/self/task/%d/hrm_%s_group", tid,
					consumer ? "consumer" : "producer");
	group_fp = fopen(file, "w");
	if (!group_fp)
		goto fopen_group_failed;
	setbuf(group_fp, NULL);
	sprintf(file, "/proc/self/task/%d/hrm_%s_counter", tid,
					consumer ? "consumer" : "producer");
	counter_fd = open(file, O_RDWR);
	if (counter_fd < 0)
		goto open_counter_failed;
	counter_fp = fopen(file, "r");
	if (!counter_fp)
		goto fopen_counter_failed;
	sprintf(file, "/proc/self/task/%d/hrm_%s_measures_goal", tid,
					consumer ? "consumer" : "producer");
	measures_goal_fd = open(file, O_RDWR);
	if (measures_goal_fd < 0)
		goto open_measures_goal_failed;
	measures_goal_fp = fopen(file, "r");
	if (!measures_goal_fp)
		goto fopen_measures_goal_failed;

	// attach thread to group
	if (fprintf(group_fp, "%d", gid) < 0)
		goto fprintf_group_failed;
	// map thread counter
	if (mmap(NULL, (size_t) page_size, PROT_READ | (consumer ? 0 : PROT_WRITE),
				MAP_SHARED, counter_fd, 0) == MAP_FAILED)
		goto mmap_counter_failed;
	fscanf(counter_fp, "%lu", &counter_address);
	// map group statistics and goal
	if (mmap(NULL, (size_t) page_size, PROT_READ | PROT_WRITE,
				MAP_SHARED, measures_goal_fd, 0) == MAP_FAILED)
		goto mmap_measures_goal_failed;
	fscanf(measures_goal_fp, "%lu %lu", &measures_address, &goal_address);

	monitor->gid = gid;
	monitor->consumer = consumer;
	monitor->counter = (struct hrm_counter *) counter_address;
	monitor->measures = (struct hrm_measures *) measures_address;
	monitor->goal = (struct hrm_goal *) goal_address;

	retval = 0;
	goto exit;

mmap_measures_goal_failed:
mmap_counter_failed:
	fprintf(group_fp, "-%d", gid);
exit:
fprintf_group_failed:
	fclose(measures_goal_fp);
fopen_measures_goal_failed:
	close(measures_goal_fd);
open_measures_goal_failed:
	fclose(counter_fp);
fopen_counter_failed:
	close(counter_fd);
open_counter_failed:
	fclose(group_fp);
fopen_group_failed:
	return retval;
}

int hrm_detach(hrm_t *monitor)
{
	int retval = -1;
	char file[256];
	FILE *group_fp;

	if (!monitor) {
		errno = EFAULT;
		return -1;
	}

	// detach thread from group
	sprintf(file, "/proc/self/task/%d/hrm_%s_group", monitor->counter->tid,
				monitor->consumer ? "consumer" : "producer");
	group_fp = fopen(file, "w");
	if (!group_fp)
		goto fopen_group_failed;
	setbuf(group_fp, NULL);
	if (fprintf(group_fp, "-%d", monitor->gid) < 0)
		goto fprintf_group_failed;

	retval = 0;

fprintf_group_failed:
	fclose(group_fp);
fopen_group_failed:
	return retval;
}

int __hrm_add_window(hrm_t *monitor, size_t window_size)
{
	int i;

	while (__sync_lock_test_and_set(&monitor->goal->goal_lock, 1));
	for(i = 0; i < HRM_MAX_WINDOWS; i++) {
		if (monitor->goal->window_size[i] == window_size)
			break;
		if (monitor->goal->window_size[i] == 0) {
			monitor->goal->window_size[i] = window_size;
			break;
		}
	}
	__sync_lock_release(&monitor->goal->goal_lock);

	if (i == HRM_MAX_WINDOWS) {
		errno = ENOMEM;
		return -1;
	}

	return i + 1;
}

int hrm_add_window(hrm_t *monitor, size_t window_size)
{
	if (!monitor) {
		errno = EFAULT;
		return -1;
	}

	return __hrm_add_window(monitor, window_size);
}

int hrm_del_window(hrm_t *monitor, size_t window_size)
{
	int i;

	if (!monitor) {
		errno = EFAULT;
		return -1;
	}
	if (monitor->consumer) {
		errno = EPERM;
		return -1;
	}
	if (window_size == 0) {
		errno = EINVAL;
		return -1;
	}
	if (window_size == monitor->goal->scope) {
		errno = EINVAL;
		return -1;
	}

	while (__sync_lock_test_and_set(&monitor->goal->goal_lock, 1));
	for(i = 0; i < HRM_MAX_WINDOWS; i++) {
		if (monitor->goal->window_size[i] == window_size) {
			monitor->goal->window_size[i] = 0;
			break;
		}
	}
	__sync_lock_release(&monitor->goal->goal_lock);

	if (i == HRM_MAX_WINDOWS) {
		errno = EINVAL;
		return -1;
	}

	return 0;
}

int hrm_set_goal(hrm_t *monitor, size_t window_size,
			double min_heart_rate, double max_heart_rate)
{
	int key = -1;

	if (!monitor) {
		errno = EFAULT;
		return -1;
	}
	if (monitor->consumer) {
		errno = EPERM;
		return -1;
	}
	if (max_heart_rate < min_heart_rate) {
		errno = EDOM;
		return -1;
	}
	if (window_size > HRM_MAX_WINDOW_SIZE) {
		errno = EDOM;
		return -1;
	}

	if (window_size > 0) {
		key = __hrm_add_window(monitor, window_size);
		if (key == -1)
			return -1;
	}

	while (__sync_lock_test_and_set(&monitor->goal->goal_lock, 1));
	monitor->goal->min_heart_rate = (uint64_t) (min_heart_rate *
							HRM_MEASURE_SCALE);
	monitor->goal->max_heart_rate = (uint64_t) (max_heart_rate *
							HRM_MEASURE_SCALE);
	monitor->goal->scope = window_size;
	__sync_lock_release(&monitor->goal->goal_lock);

	return key;
}

int hrm_unset_goal(hrm_t *monitor)
{
	if (!monitor) {
		errno = EFAULT;
		return -1;
	}
	if (monitor->consumer) {
		errno = EPERM;
		return -1;
	}

	while (__sync_lock_test_and_set(&monitor->goal->goal_lock, 1));
	monitor->goal->min_heart_rate = 0;
	monitor->goal->max_heart_rate = 0;
	monitor->goal->scope = 0;
	__sync_lock_release(&monitor->goal->goal_lock);

	return 0;
}

int hrm_get_windows_number(hrm_t *monitor)
{
	int count = 0;

	while (__sync_lock_test_and_set(&monitor->goal->goal_lock, 1));
	for (int i = 0; i < HRM_MAX_WINDOWS - 1; i++)
		if (monitor->goal->window_size[i])
			count++;
	__sync_lock_release(&monitor->goal->goal_lock);

	return count;
}

size_t hrm_get_window_size(hrm_t *monitor, int key)
{
	if (!monitor) {
		errno = EFAULT;
		return HRM_MAX_WINDOW_SIZE;
	}
	if (key < 0 || key > HRM_MAX_WINDOWS) {
		errno = EINVAL;
		return HRM_MAX_WINDOW_SIZE;
	}

	if (key == 0)
		return 0;

	return monitor->goal->window_size[key - 1];

}

double hrm_seek_heart_rate(const hrm_t *monitor, size_t window_size, int *key)
{
	int i;
	double hr = 0;

	if (!monitor) {
		errno = EFAULT;
		*key = -1;
		return 0;
	}
	if (window_size > HRM_MAX_WINDOW_SIZE) {
		errno = EINVAL;
		*key = -1;
		return 0;
	}

	if (window_size == 0) {
		*key = 0;
		while (__sync_lock_test_and_set(&monitor->goal->goal_lock, 1));
		if (monitor->measures->global.time)
			hr = ((double) monitor->measures->global.count /
				(double) monitor->measures->global.time) *
								NSEC_PER_SEC;
		__sync_lock_release(&monitor->goal->goal_lock);

		return hr;
	}

	while (__sync_lock_test_and_set(&monitor->goal->goal_lock, 1));
	for(i = 0; i < HRM_MAX_WINDOWS; i++)
		if (monitor->goal->window_size[i] == window_size) {
			*key = i + 1;
			break;
		}
	if (monitor->measures->window[i].time)
		hr = ((double) monitor->measures->window[i].count /
			(double) monitor->measures->window[i].time) *
								NSEC_PER_SEC;
	__sync_lock_release(&monitor->goal->goal_lock);

	if (i == HRM_MAX_WINDOWS) {
		errno = EINVAL;
		*key = -1;
		return 0;
	}

	return hr;
}

double hrm_get_heart_rate(const hrm_t *monitor, size_t *window_size, int key)
{
	double hr = 0;

	if (!monitor) {
		errno = EFAULT;
		*window_size = HRM_MAX_WINDOW_SIZE;
		return 0;
	}
	if (key < 0 || key > HRM_MAX_WINDOWS) {
		errno = EINVAL;
		*window_size = HRM_MAX_WINDOW_SIZE;
		return 0;
	}

	if (key == 0) {
		*window_size = 0;
		while (__sync_lock_test_and_set(&monitor->goal->goal_lock, 1));
		if (monitor->measures->global.time)
			hr = ((double) monitor->measures->global.count /
				(double) monitor->measures->global.time) *
								NSEC_PER_SEC;
		__sync_lock_release(&monitor->goal->goal_lock);

		return hr;
	}

	while (__sync_lock_test_and_set(&monitor->goal->goal_lock, 1));
	*window_size = monitor->goal->window_size[key - 1];
	if (!*window_size) {
		errno = EINVAL;
		*window_size = HRM_MAX_WINDOW_SIZE;
	} else if (monitor->measures->window[key - 1].time)
		hr = ((double) monitor->measures->window[key - 1].count /
			(double) monitor->measures->window[key - 1].time) *
								NSEC_PER_SEC;
	__sync_lock_release(&monitor->goal->goal_lock);

	return hr;
}

double
hrm_get_min_heart_rate(const hrm_t *monitor, size_t *window_size)
{
	double hr;

	if (!monitor) {
		errno = EFAULT;
		*window_size = HRM_MAX_WINDOW_SIZE;
		return 0;
	}

	while (__sync_lock_test_and_set(&monitor->goal->goal_lock, 1));
	*window_size = monitor->goal->scope;
	hr = (double) monitor->goal->min_heart_rate / HRM_MEASURE_SCALE;
	__sync_lock_release(&monitor->goal->goal_lock);

	return hr;
}

double
hrm_get_max_heart_rate(const hrm_t *monitor, size_t *window_size)
{
	double hr;

	if (!monitor) {
		errno = EFAULT;
		*window_size = HRM_MAX_WINDOW_SIZE;
		return 0;
	}

	while (__sync_lock_test_and_set(&monitor->goal->goal_lock, 1));
	*window_size = monitor->goal->scope;
	hr = (double) monitor->goal->max_heart_rate / HRM_MEASURE_SCALE;
	__sync_lock_release(&monitor->goal->goal_lock);

	return hr;
}

int heartbeat(hrm_t *monitor, uint64_t n)
{
	if (!monitor) {
		errno = EFAULT;
		return -1;
	}
	if (monitor->consumer) {
		errno = EPERM;
		return -1;
	}

	__sync_add_and_fetch(&monitor->counter->counter, n);

	return 0;
}

int hrm_get_tids(const hrm_t *monitor, pid_t tids[], int n)
{
	pid_t *tids_;
	int tids_count = 0;
	struct hrm_counter *counter;

	if (!monitor) {
		errno = EFAULT;
		return -1;
	}
	if (!monitor->consumer) {
		errno = EPERM;
		return -1;
	}
	if (tids == NULL)
		return -1;

	tids_ = (pid_t *) calloc(64, sizeof(pid_t));
	if (tids_ == NULL)
		return -1;
	for (int i = 0; i < 64; i++) {
		counter = (struct hrm_counter *)
				((unsigned long) monitor->counter + i * 64);
		if (counter->used)
			tids_[tids_count++] = counter->tid;
	}

	memcpy(tids, tids_, (tids_count < n ? tids_count : n) * sizeof(pid_t));
	tids[(tids_count < n ? tids_count : n)] = 0;

	return 0;
}

