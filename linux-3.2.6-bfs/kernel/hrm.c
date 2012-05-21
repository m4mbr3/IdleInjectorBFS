#include <asm/mman.h>
#include <linux/err.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <linux/hrm.h>

struct hrm_memory_map {
	pid_t pid;
	unsigned long user_address;

	int references;

	struct list_head link;
};

LIST_HEAD(hrm_groups);
DEFINE_SPINLOCK(hrm_groups_lock);

static enum hrtimer_restart __hrm_update_group_measures(struct hrtimer *timer)
{
	struct hrm_group *group;
	struct hrm_measures *measures;
	struct hrm_goal *goal;
	unsigned long members_lock_flags;
	struct timespec current_time;
	u64 heartbeats = 0;
	struct hrm_producer *producer;
	struct timespec elapsed_time, elapsed_time_w;
	u64 heartbeats_w = 0;
	size_t ws;
	int window_first;
	int i;

	group = container_of(timer, struct hrm_group, timer);
	measures = (struct hrm_measures *) HRM_MEASURES_ADDR(group->measures_goal.kernel_address);
	goal = (struct hrm_goal *) HRM_GOAL_ADDR(group->measures_goal.kernel_address);

	read_lock_irqsave(&group->members_lock, members_lock_flags);

	hrtimer_start(&group->timer, ktime_set(0, (unsigned long)
			NSEC_PER_USEC * HRM_TIMER_PERIOD), HRTIMER_MODE_REL);

	getrawmonotonic(&current_time);
	elapsed_time = timespec_sub(current_time, group->timestamp);
	measures->global.time = (u64) timespec_to_ns(&elapsed_time);

	heartbeats = group->history.history;
	list_for_each_entry (producer, &group->producers, group_link) {
		heartbeats += producer->counter->counter;
	}
	measures->global.count = heartbeats;

	group->history.window[group->history.window_cur].counter = heartbeats;
	group->history.window[group->history.window_cur].elapsed_time = elapsed_time;

	if (unlikely(group->history.buffered == 0))
		goto empty_buffer;

	for (i = 0; i < HRM_MAX_WINDOWS; i++) {
		ws = goal->window_size[i];
		if (ws != 0) {
			if (ws > group->history.buffered)
				ws = group->history.buffered;

			window_first =
				(group->history.window_cur - ws) &
							(HRM_MAX_WINDOW_SIZE - 1);

			heartbeats_w = heartbeats -
				group->history.window[window_first].counter;
			elapsed_time_w =
				timespec_sub(elapsed_time,
				group->history.window[window_first].elapsed_time);
			measures->window[i].count = heartbeats_w;
			measures->window[i].time = (u64)timespec_to_ns(&elapsed_time_w);
		}
	}

empty_buffer:
	if (group->history.buffered < HRM_MAX_WINDOW_SIZE)
		group->history.buffered++;
	group->history.window_cur =
			(group->history.window_cur + 1) & (HRM_MAX_WINDOW_SIZE - 1);

	read_unlock_irqrestore(&group->members_lock, members_lock_flags);

	return HRTIMER_NORESTART;
}

static struct hrm_group *__hrm_create_group(int gid)
{
	struct hrm_group *group;

	group = (struct hrm_group *) kzalloc(sizeof(struct hrm_group), GFP_KERNEL);
	if (group == NULL) {
		printk(KERN_ERR __FILE__ " @ %d kzalloc() failed\n", __LINE__);
		group = ERR_PTR(-ENOMEM);
		goto failure_kzalloc_group;
	}

	group->counters.kernel_address = __get_free_pages(GFP_KERNEL | __GFP_ZERO, HRM_GROUP_ORDER);
	if (group->counters.kernel_address == 0) {
		printk(KERN_ERR __FILE__ " @ %d __get_free_pages() failed\n", __LINE__);
		group = ERR_PTR(-ENOMEM);
		goto failure_get_free_pages;
	}
	group->counters.size = HRM_GROUP_SIZE * L1_CACHE_BYTES;
	INIT_LIST_HEAD(&group->counters.maps);

	group->measures_goal.kernel_address = get_zeroed_page(GFP_KERNEL);
	if (group->measures_goal.kernel_address == 0) {
		printk(KERN_ERR __FILE__ " @ %d get_zeroed_page() failed\n", __LINE__);
		group = ERR_PTR(-ENOMEM);
		goto failure_get_zeroed_page;
	}
	group->measures_goal.size = PAGE_SIZE;
	INIT_LIST_HEAD(&group->measures_goal.maps);

	group->gid = gid;

	bitmap_zero(group->counters_allocation, HRM_GROUP_SIZE);

	hrtimer_init(&group->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	group->timer.function = __hrm_update_group_measures;

	INIT_LIST_HEAD(&group->producers);
	INIT_LIST_HEAD(&group->consumers);
	group->members_lock = __RW_LOCK_UNLOCKED(group->members_lock);

	INIT_LIST_HEAD(&group->link);

	return group;

failure_get_zeroed_page:
	free_pages(group->counters.kernel_address, HRM_GROUP_ORDER);
failure_get_free_pages:
	kfree(group);
failure_kzalloc_group:
	return group;
}

static int __hrm_destroy_group(struct hrm_group *group)
{
	hrtimer_cancel(&group->timer);

	free_page(group->measures_goal.kernel_address);
	free_pages(group->counters.kernel_address, HRM_GROUP_ORDER);
	kfree(group);

	return 0;
}

static int __hrm_add_group(struct hrm_group *group)
{
	list_add(&group->link, &hrm_groups);

	return 0;
}

static int __hrm_delete_group(struct hrm_group *group)
{
	list_del(&group->link);

	return 0;
}

static struct hrm_group *__hrm_find_group(int gid)
{
	struct hrm_group *group;

	list_for_each_entry (group, &hrm_groups, link) {
		if (group->gid == gid)
			return group;
	}

	return NULL;
}

static int __hrm_allocate_group_counter(struct hrm_group *group)
{
	int counter_index = 0;

	counter_index = bitmap_find_free_region(group->counters_allocation, HRM_GROUP_SIZE, 0);
	if (counter_index < 0) {
		printk(KERN_ERR __FILE__ " @ %d bitmap_find_free_region() failed\n", __LINE__);
		return counter_index;
	}

	return counter_index;
}

static int __hrm_release_group_counter(struct hrm_group *group, int counter_index)
{
	bitmap_release_region(group->counters_allocation, counter_index, 0);

	return 0;
}

struct hrm_producer *__hrm_find_producer_struct(struct task_struct *task, int gid)
{
	struct hrm_producer *producer;

	list_for_each_entry (producer, &task->hrm_producers, task_link) {
		if (producer->group->gid == gid)
			return producer;
	}

	return NULL;
}

int hrm_add_producer_task_to_group(struct task_struct *task, int gid)
{
	int retval = 0;
	struct hrm_producer *producer;
	struct hrm_group *group;
	int group_exists = 0;
	unsigned long members_lock_flags;
	int counter_index;
	struct timespec current_time;

	if (task == NULL) {
		printk(KERN_ERR __FILE__ " @ %d task not valid\n", __LINE__);
		return -EINVAL;
	}

	if (__hrm_find_producer_struct(task, gid) != NULL) {
		printk(KERN_ERR __FILE__ " @ %d task %d already enabled for group %d\n", __LINE__, (int) task->pid, gid);
		return -EINVAL;
	}

	producer = (struct hrm_producer *) kzalloc(sizeof(struct hrm_producer), GFP_KERNEL);
	if (producer == NULL) {
		printk(KERN_ERR __FILE__ " @ %d kzalloc() failed\n", __LINE__);
		return -ENOMEM;
	}

	spin_lock(&hrm_groups_lock);

	group = __hrm_find_group(gid);
	if (group == NULL) {
		group = __hrm_create_group(gid);
		if (IS_ERR(group)) {
			printk(KERN_ERR __FILE__ " @ %d __hrm_create_group() failed\n", __LINE__);
			retval = PTR_ERR(group);
			goto failure_create_group;
		}
	} else {
		group_exists = 1;
	}

	counter_index = __hrm_allocate_group_counter(group);
	if (counter_index < 0) {
		printk(KERN_ERR __FILE__ " @ %d __hrm_allocate_group_counter() failed\n", __LINE__);
		retval = counter_index;
		goto failure_allocate_group_counter;
	}

	producer->task = task;
	producer->counter_index = counter_index;
	producer->counter = (struct hrm_counter *) HRM_COUNTER_ADDR(group->counters.kernel_address, counter_index);
	producer->counter->tid = task->pid;
	producer->counter->used = 1;
	producer->measures = (struct hrm_measures *) HRM_MEASURES_ADDR(group->measures_goal.kernel_address);
	producer->goal = (struct hrm_goal *) HRM_GOAL_ADDR(group->measures_goal.kernel_address);
	producer->group = group;
	INIT_LIST_HEAD(&producer->task_link);
	INIT_LIST_HEAD(&producer->group_link);

	write_lock_irqsave(&group->members_lock, members_lock_flags);
	list_add(&producer->task_link, &task->hrm_producers);
	list_add(&producer->group_link, &group->producers);
	write_unlock_irqrestore(&group->members_lock, members_lock_flags);

	if (!timespec_to_ns(&group->timestamp)) {
		if (!group_exists)
			__hrm_add_group(group);
		getrawmonotonic(&current_time);
		group->timestamp = current_time;
		group->history.window_cur = 1;
		group->history.buffered = 1;
		hrtimer_start(&group->timer, ktime_set(0, NSEC_PER_USEC * HRM_TIMER_PERIOD), HRTIMER_MODE_REL);
	}

	spin_unlock(&hrm_groups_lock);

	return retval;

failure_allocate_group_counter:
	if (!group_exists)
		__hrm_destroy_group(group);
failure_create_group:
	spin_unlock(&hrm_groups_lock);
	kfree(producer);

	return retval;
}

int hrm_delete_producer_task_from_group(struct task_struct *task, int gid)
{
	struct hrm_producer *producer;
	struct hrm_group *group;
	unsigned long members_lock_flags;

	if (task == NULL) {
		printk(KERN_ERR __FILE__ " @ %d task not valid\n", __LINE__);
		return -EINVAL;
	}

	producer = __hrm_find_producer_struct(task, gid);
	if (producer == NULL) {
		printk(KERN_ERR __FILE__ " @ %d task %d not enabled for group %d\n", __LINE__, (int) task->pid, gid);
		return -EINVAL;
	}
	group = producer->group;
	producer->counter->used = 0;
	group->history.history += producer->counter->counter;
	__hrm_release_group_counter(group, producer->counter_index);

	spin_lock(&hrm_groups_lock);
	write_lock_irqsave(&group->members_lock, members_lock_flags);
	list_del(&producer->task_link);
	list_del(&producer->group_link);
	kfree(producer);
	__hrm_put_group_memory_map(task, &group->counters);
	__hrm_put_group_memory_map(task, &group->measures_goal);
	if (list_empty(&group->producers) && list_empty(&group->consumers)) {
		__hrm_delete_group(group);
		write_unlock_irqrestore(&group->members_lock, members_lock_flags);
		__hrm_destroy_group(group);
	} else {
		write_unlock_irqrestore(&group->members_lock, members_lock_flags);
	}
	spin_unlock(&hrm_groups_lock);

	return 0;
}

int hrm_delete_producer_task_from_all_groups(struct task_struct *task)
{
	struct hrm_producer *producer;
	struct hrm_producer *producer_fallback;
	struct hrm_group *group;
	unsigned long members_lock_flags;

	if (task == NULL) {
		printk(KERN_ERR __FILE__ " @ %d task not valid\n", __LINE__);
		return -EINVAL;
	}

	if (!hrm_producer_task_enabled(task)) {
		printk(KERN_ERR __FILE__ " @ %d task %d not enabled\n", __LINE__, (int) task->pid);
		return -EINVAL;
	}

	spin_lock(&hrm_groups_lock);
	list_for_each_entry_safe (producer, producer_fallback, &task->hrm_producers, task_link) {
		group = producer->group;
		group->history.history += producer->counter->counter;
		__hrm_release_group_counter(group, producer->counter_index);
		write_lock_irqsave(&group->members_lock, members_lock_flags);
		list_del(&producer->task_link);
		list_del(&producer->group_link);
		kfree(producer);
		__hrm_put_group_memory_map(task, &group->counters);
		__hrm_put_group_memory_map(task, &group->measures_goal);
		if (list_empty(&group->producers) && list_empty(&group->consumers)) {
			__hrm_delete_group(group);
			write_unlock_irqrestore(&group->members_lock, members_lock_flags);
			__hrm_destroy_group(group);
		} else {
			write_unlock_irqrestore(&group->members_lock, members_lock_flags);
		}
	}
	spin_unlock(&hrm_groups_lock);

	return 0;
}

int hrm_producer_task_enabled_group(struct task_struct *task, int gid)
{
	if (task == NULL) {
		printk(KERN_ERR __FILE__ " @ %d task not valid\n", __LINE__);
		return -EINVAL;
	}

	return __hrm_find_producer_struct(task, gid) != NULL;
}

int hrm_producer_task_enabled(struct task_struct *task)
{
	if (task == NULL) {
		printk(KERN_ERR __FILE__ " @ %d task not valid\n", __LINE__);
		return -EINVAL;
	}

	return !list_empty(&task->hrm_producers);
}

struct hrm_consumer *__hrm_find_consumer_struct(struct task_struct *task, int gid)
{
	struct hrm_consumer *consumer;

	list_for_each_entry (consumer, &task->hrm_consumers, task_link) {
		if (consumer->group->gid == gid)
			return consumer;
	}

	return NULL;
}

int hrm_add_consumer_task_to_group(struct task_struct *task, int gid)
{
	int retval = 0;
	struct hrm_consumer *consumer;
	struct hrm_group *group;
	int group_exists = 0;
	unsigned long members_lock_flags;

	if (task == NULL) {
		printk(KERN_ERR __FILE__ " @ %d task not valid\n", __LINE__);
		return -EINVAL;
	}

	if (__hrm_find_consumer_struct(task, gid) != NULL) {
		printk(KERN_ERR __FILE__ " @ %d task %d already enabled for group %d\n", __LINE__, (int) task->pid, gid);
		return -EINVAL;
	}

	consumer = (struct hrm_consumer *) kzalloc(sizeof(struct hrm_consumer), GFP_KERNEL);
	if (consumer == NULL) {
		printk(KERN_ERR __FILE__ " @ %d kzalloc() failed\n", __LINE__);
		return -ENOMEM;
	}

	spin_lock(&hrm_groups_lock);

	group = __hrm_find_group(gid);
	if (group == NULL) {
		group = __hrm_create_group(gid);
		if (IS_ERR(group)) {
			printk(KERN_ERR __FILE__ " @ %d __hrm_create_group() failed\n", __LINE__);
			retval = PTR_ERR(group);
			goto failure_create_group;
		}
	} else {
		group_exists = 1;
	}

	consumer->group = group;
	INIT_LIST_HEAD(&consumer->task_link);
	INIT_LIST_HEAD(&consumer->group_link);

	write_lock_irqsave(&group->members_lock, members_lock_flags);
	list_add(&consumer->task_link, &task->hrm_consumers);
	list_add(&consumer->group_link, &group->consumers);
	write_unlock_irqrestore(&group->members_lock, members_lock_flags);

	if (!group_exists)
		__hrm_add_group(group);

	spin_unlock(&hrm_groups_lock);

	return retval;

failure_create_group:
	spin_unlock(&hrm_groups_lock);
	kfree(consumer);

	return retval;
}

int hrm_delete_consumer_task_from_group(struct task_struct *task, int gid)
{
	struct hrm_consumer *consumer;
	struct hrm_group *group;
	unsigned long members_lock_flags;

	if (task == NULL) {
		printk(KERN_ERR __FILE__ " @ %d task not valid\n", __LINE__);
		return -EINVAL;
	}

	consumer = __hrm_find_consumer_struct(task, gid);
	if (consumer == NULL) {
		printk(KERN_ERR __FILE__ " @ %d task %d not enabled for group %d\n", __LINE__, (int) task->pid, gid);
		return -EINVAL;
	}
	group = consumer->group;

	spin_lock(&hrm_groups_lock);
	write_lock_irqsave(&group->members_lock, members_lock_flags);
	list_del(&consumer->task_link);
	list_del(&consumer->group_link);
	kfree(consumer);
	__hrm_put_group_memory_map(task, &group->counters);
	__hrm_put_group_memory_map(task, &group->measures_goal);
	if (list_empty(&group->producers) && list_empty(&group->consumers)) {
		__hrm_delete_group(group);
		write_unlock_irqrestore(&group->members_lock, members_lock_flags);
		__hrm_destroy_group(group);
	} else {
		write_unlock_irqrestore(&group->members_lock, members_lock_flags);
	}
	spin_unlock(&hrm_groups_lock);

	return 0;
}

int hrm_delete_consumer_task_from_all_groups(struct task_struct *task)
{
	struct hrm_consumer *consumer;
	struct hrm_consumer *consumer_fallback;
	struct hrm_group *group;
	unsigned long members_lock_flags;

	if (task == NULL) {
		printk(KERN_ERR __FILE__ " @ %d task not valid\n", __LINE__);
		return -EINVAL;
	}

	if (!hrm_consumer_task_enabled(task)) {
		printk(KERN_ERR __FILE__ " @ %d task %d not enabled\n", __LINE__, (int) task->pid);
		return -EINVAL;
	}

	spin_lock(&hrm_groups_lock);
	list_for_each_entry_safe (consumer, consumer_fallback, &task->hrm_consumers, task_link) {
		group = consumer->group;
		write_lock_irqsave(&group->members_lock, members_lock_flags);
		list_del(&consumer->task_link);
		list_del(&consumer->group_link);
		kfree(consumer);
		__hrm_put_group_memory_map(task, &group->counters);
		__hrm_put_group_memory_map(task, &group->measures_goal);
		if (list_empty(&group->producers) && list_empty(&group->consumers)) {
			__hrm_delete_group(group);
			write_unlock_irqrestore(&group->members_lock, members_lock_flags);
			__hrm_destroy_group(group);
		} else {
			write_unlock_irqrestore(&group->members_lock, members_lock_flags);
		}
	}
	spin_unlock(&hrm_groups_lock);

	return 0;
}

int hrm_consumer_task_enabled_group(struct task_struct *task, int gid)
{
	if (task == NULL) {
		printk(KERN_ERR __FILE__ " @ %d task not valid\n", __LINE__);
		return -EINVAL;
	}

	return __hrm_find_consumer_struct(task, gid) != NULL;
}

int hrm_consumer_task_enabled(struct task_struct *task)
{
	if (task == NULL) {
		printk(KERN_ERR __FILE__ " @ %d task not valid\n", __LINE__);
		return -EINVAL;
	}

	return !list_empty(&task->hrm_consumers);
}

int __hrm_add_group_memory_map(struct task_struct *task, struct hrm_memory *memory, unsigned long user_address)
{
	struct hrm_memory_map *memory_map;

	memory_map = (struct hrm_memory_map *) kzalloc(sizeof(struct hrm_memory_map), GFP_KERNEL);
	if (memory_map == NULL) {
		printk(KERN_ERR __FILE__ " @ %d kzalloc() failed\n", __LINE__);
		return -ENOMEM;
	}
	memory_map->pid = task->tgid;
	memory_map->user_address = user_address;
	memory_map->references = 1;
	INIT_LIST_HEAD(&memory_map->link);
	list_add(&memory_map->link, &memory->maps);
	return 0;
}

int __hrm_get_group_memory_map(struct task_struct *task, struct hrm_memory *memory)
{
	struct hrm_memory_map *memory_map;

	list_for_each_entry (memory_map, &memory->maps, link) {
		if (memory_map->pid == task->tgid) {
			memory_map->references++;
			return 0;
		}
	}
	return -ESRCH;
}

int __hrm_put_group_memory_map(struct task_struct *task, struct hrm_memory *memory)
{
	struct hrm_memory_map *memory_map;
	struct hrm_memory_map *memory_map_fallback;

	list_for_each_entry_safe (memory_map, memory_map_fallback, &memory->maps, link) {
		if (memory_map->pid == task->tgid) {
			memory_map->references--;
			if (memory_map->references == 0) {
				list_del(&memory_map->link);
				kfree(memory_map);
			}
			return 0;
		}
	}
	return -ESRCH;
}

unsigned long __hrm_find_group_memory_map(struct task_struct *task, struct hrm_memory *memory)
{
	struct hrm_memory_map *memory_map;

	list_for_each_entry (memory_map, &memory->maps, link) {
		if (memory_map->pid == task->tgid)
			return memory_map->user_address;
	}
	return 0;
}

static inline u64 __hrm_compute_hr(u64 count, u64 time)
{
	return (count * USEC_PER_SEC) / (time / MSEC_PER_SEC / HRM_MEASURE_SCALE);

}

u64
hrm_seek_heart_rate(const struct hrm_group* group, size_t window_size, int *key)
{
	struct hrm_goal *goal = NULL;
	struct hrm_measures *measures;
	u64 hr = 0;
	int i;

	if (!group) {
		*key = -EFAULT;
		goto exit;
	}
	if (window_size > HRM_MAX_WINDOW_SIZE) {
		*key = -EINVAL;
		goto exit;
	}

	goal = (struct hrm_goal *)
			HRM_GOAL_ADDR(group->measures_goal.kernel_address);
	measures = (struct hrm_measures *)
			HRM_MEASURES_ADDR(group->measures_goal.kernel_address);

	if (window_size == 0) {
		*key = 0;
		while(__sync_lock_test_and_set(&goal->goal_lock, 1));
		if (measures->global.time)
			hr = __hrm_compute_hr(measures->global.count,
							measures->global.time);
		__sync_lock_release(&goal->goal_lock);

		goto exit;
	}

	while(__sync_lock_test_and_set(&goal->goal_lock, 1));
	for (i = 0; i < HRM_MAX_WINDOWS; i++)
		if (goal->window_size[i] == window_size &&
						measures->window[i].time) {
			*key = i + 1;
			hr = __hrm_compute_hr(measures->window[i].count,
						measures->window[i].time);
			break;
		}
	__sync_lock_release(&goal->goal_lock);

	if (i == HRM_MAX_WINDOWS)
		*key = -EINVAL;

exit:
	return hr;
}

u64
hrm_get_heart_rate(const struct hrm_group* group, size_t *window_size, int key)
{
	struct hrm_goal *goal = NULL;
	struct hrm_measures *measures;
	u64 hr = 0;

	if (!group) {
		*window_size = HRM_MAX_WINDOW_SIZE;
		goto exit;
	}
	if (key < 0 || key > HRM_MAX_WINDOWS) {
		*window_size = HRM_MAX_WINDOW_SIZE;
		goto exit;
	}

	goal = (struct hrm_goal *)
			HRM_GOAL_ADDR(group->measures_goal.kernel_address);
	measures = (struct hrm_measures *)
			HRM_MEASURES_ADDR(group->measures_goal.kernel_address);

	if (key == 0) {
		*window_size = 0;
		while(__sync_lock_test_and_set(&goal->goal_lock, 1));
		if (measures->global.time)
			hr = __hrm_compute_hr(measures->global.count,
							measures->global.time);
		__sync_lock_release(&goal->goal_lock);

		goto exit;
	}

	while(__sync_lock_test_and_set(&goal->goal_lock, 1));
	*window_size = goal->window_size[key - 1];
	if (*window_size != 0 && measures->window[key - 1].time)
		hr = __hrm_compute_hr(measures->window[key - 1].count,
					measures->window[key - 1].time);
	else
		*window_size = HRM_MAX_WINDOW_SIZE;
	__sync_lock_release(&goal->goal_lock);

exit:
	return hr;

}

u64 hrm_get_min_heart_rate(const struct hrm_group* group, size_t *window_size)
{
	struct hrm_goal *goal = NULL;
	u64 hr = 0;

	if (!group) {
		*window_size = HRM_MAX_WINDOW_SIZE;
		goto exit;
	}

	goal = (struct hrm_goal *)
			HRM_GOAL_ADDR(group->measures_goal.kernel_address);
	while(__sync_lock_test_and_set(&goal->goal_lock, 1));
	*window_size = goal->scope;
	hr = goal->min_heart_rate;
	__sync_lock_release(&goal->goal_lock);

exit:
	return hr;
}

u64 hrm_get_max_heart_rate(const struct hrm_group* group, size_t *window_size)
{
	struct hrm_goal *goal = NULL;
	u64 hr = 0;

	if (!group) {
		*window_size = HRM_MAX_WINDOW_SIZE;
		goto exit;
	}

	goal = (struct hrm_goal *)
			HRM_GOAL_ADDR(group->measures_goal.kernel_address);
	while(__sync_lock_test_and_set(&goal->goal_lock, 1));
	*window_size = goal->scope;
	hr = goal->max_heart_rate;
	__sync_lock_release(&goal->goal_lock);

exit:
	return hr;
}

