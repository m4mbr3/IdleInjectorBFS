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
#ifndef _LINUX_HRM_H
#define _LINUX_HRM_H

#define HRM_MAX_WINDOWS 32
#define HRM_MEASURE_SCALE 1000

#ifndef __KERNEL__
#define NSEC_PER_SEC 1000000000

typedef uint64_t u64;
typedef uint32_t u32;
typedef int64_t s64;
typedef int32_t s32;
#endif

struct hrm_counter {
	pid_t tid;
	int used;

	u64 counter;
};

struct hrm_measure {
	u64 count;
	u64 time;
};

struct hrm_measures {
	struct hrm_measure global;
	struct hrm_measure window[HRM_MAX_WINDOWS];
};

struct hrm_goal {
	int goal_lock;
	u64 min_heart_rate;
	u64 max_heart_rate;
	size_t scope;

	size_t window_size[HRM_MAX_WINDOWS];
};

#ifdef __KERNEL__

#include <asm/cache.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/types.h>

#define HRM_PAGE_GROUP_SIZE (PAGE_SIZE / L1_CACHE_BYTES)

#define HRM_GROUP_ORDER (CONFIG_HRM_GROUP_ORDER)
#define HRM_GROUP_SIZE ((1 << HRM_GROUP_ORDER) * HRM_PAGE_GROUP_SIZE)
#define HRM_MAX_WINDOW_SIZE (1 << CONFIG_HRM_MAX_WINDOW_SIZE)
#define HRM_TIMER_PERIOD (CONFIG_HRM_TIMER_PERIOD)

#define HRM_COUNTER_ADDR(address, index) ((unsigned long) (address) + L1_CACHE_BYTES * (index))
#define HRM_COUNTERS_ADDR(address) ((unsigned long) (address))
#define HRM_MEASURES_ADDR(address) ((unsigned long) (address))
#define HRM_GOAL_ADDR(address) ((unsigned long) (address) + PAGE_SIZE - \
	sizeof(struct hrm_goal))

extern struct list_head hrm_groups;
extern spinlock_t hrm_groups_lock;

struct hrm_memory {
	unsigned long kernel_address;
	size_t size;

	struct list_head maps;
};

struct hrm_window_span {
		int begin;
		int end;
};

struct hrm_group {
	int gid;

	struct hrm_memory counters;
	struct hrm_memory measures_goal;

	struct {
		int window_cur;
		int buffered;

		struct {
			u64 counter;
			struct timespec elapsed_time;
		} window[HRM_MAX_WINDOW_SIZE];

		u64 history;
	} history;

	DECLARE_BITMAP(counters_allocation, HRM_GROUP_SIZE);

	struct hrtimer timer;

	struct timespec timestamp;

	struct list_head producers;
	struct list_head consumers;
	rwlock_t members_lock;

	struct list_head link;
};

struct hrm_producer {
	struct task_struct *task;

	int counter_index;

	struct hrm_counter *counter;
	struct hrm_measures *measures;
	struct hrm_goal *goal;

	struct hrm_group *group;

	unsigned long counter_user_address;
	unsigned long measures_user_address;
	unsigned long goal_user_address;

	struct list_head task_link;
	struct list_head group_link;
};

struct hrm_consumer {
	struct hrm_group *group;

	unsigned long counter_user_address;
	unsigned long measures_user_address;
	unsigned long goal_user_address;

	struct list_head task_link;
	struct list_head group_link;
};

struct hrm_producer *__hrm_find_producer_struct(struct task_struct *task,
								int gid);
int hrm_add_producer_task_to_group(struct task_struct *task, int gid);
int hrm_delete_producer_task_from_group(struct task_struct *task, int gid);
int hrm_delete_producer_task_from_all_groups(struct task_struct *task);
int hrm_producer_task_enabled_group(struct task_struct *task, int gid);
int hrm_producer_task_enabled(struct task_struct *task);

struct hrm_consumer *__hrm_find_consumer_struct(struct task_struct *task,
								int gid);
int hrm_add_consumer_task_to_group(struct task_struct *task, int gid);
int hrm_delete_consumer_task_from_group(struct task_struct *task, int gid);
int hrm_delete_consumer_task_from_all_groups(struct task_struct *task);
int hrm_consumer_task_enabled_group(struct task_struct *task, int gid);
int hrm_consumer_task_enabled(struct task_struct *task);

int __hrm_add_group_memory_map(struct task_struct *task,
		struct hrm_memory *memory, unsigned long user_address);
int __hrm_get_group_memory_map(struct task_struct *task,
						struct hrm_memory *memory);
int __hrm_put_group_memory_map(struct task_struct *task,
						struct hrm_memory *memory);
unsigned long __hrm_find_group_memory_map(struct task_struct *task,
						struct hrm_memory *memory);

ssize_t hrm_producer_group_write(struct file *file, const char __user *buf,
						size_t size, loff_t *off);
ssize_t hrm_consumer_group_write(struct file *file, const char __user *buf,
						size_t size, loff_t *off);
ssize_t hrm_producer_counter_read(struct file *file, char __user *buf,
						size_t size, loff_t *off);
int hrm_producer_counter_mmap(struct file* file, struct vm_area_struct *vma);
ssize_t hrm_consumer_counter_read(struct file *file, char __user *buf,
						size_t size, loff_t *off);
int hrm_consumer_counter_mmap(struct file* file, struct vm_area_struct *vma);
ssize_t hrm_producer_measures_goal_read(struct file *file, char __user *buf,
						size_t size, loff_t *off);
int hrm_producer_measures_goal_mmap(struct file* file,
						struct vm_area_struct *vma);
ssize_t hrm_consumer_measures_goal_read(struct file *file, char __user *buf,
						size_t size, loff_t *off);
int hrm_consumer_measures_goal_mmap(struct file* file,
						struct vm_area_struct *vma);

/* Kernelspace consumer API */
u64
hrm_seek_heart_rate(const struct hrm_group* group, size_t window_size, int *key);
u64
hrm_get_heart_rate(const struct hrm_group* group, size_t *window_size, int key);
u64 hrm_get_min_heart_rate(const struct hrm_group* group, size_t *window_size);
u64 hrm_get_max_heart_rate(const struct hrm_group* group, size_t *window_size);
/* */

#endif

#endif /* _LINUX_HRM_H_ */

