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
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/hrm.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>

#include "internal.h"

ssize_t hrm_producer_group_write(struct file *file, const char __user *buf, size_t size, loff_t *off)
{
	ssize_t retval;
	struct task_struct *task;
	char kbuf[1024];
	int gid;

	task = get_proc_task(file->f_dentry->d_inode);

	if (task != current) {
		printk(KERN_ERR __FILE__ " @ %d task %d not valid\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_valid;
	}

	memset(kbuf, 0, sizeof(char) * 1024);
	if (copy_from_user(kbuf, buf, size) != 0) {
		printk(KERN_ERR __FILE__ " @ %d copy_from_user() failed\n", __LINE__);
		retval = -EFAULT;
		goto failure_copy_from_user;
	}

	gid = (int) simple_strtol(kbuf, NULL, 10);
	if (gid > 0) {
		retval = hrm_add_producer_task_to_group(task, gid);
		if (retval < 0) {
			printk(KERN_ERR __FILE__ " @ %d hrm_add_producer_task_to_group() failed\n", __LINE__);
			goto failure_add_producer_task_to_group;
		}
	} else if (gid < 0) {
		gid = -gid;
		retval = hrm_delete_producer_task_from_group(task, gid);
		if (retval < 0) {
			printk(KERN_ERR __FILE__ " @ %d hrm_delete_producer_task_from_group() failed\n", __LINE__);
			goto failure_delete_producer_task_from_group;
		}
	} else {
		printk(KERN_ERR __FILE__ " @ %d group 0 not valid\n", __LINE__);
		retval = -EINVAL;
		goto failure_group_not_valid;
	}

	retval = (ssize_t) size;

failure_group_not_valid:
failure_add_producer_task_to_group:
failure_delete_producer_task_from_group:
failure_copy_from_user:
failure_task_not_valid:
	put_task_struct(task);

	return retval;
}

ssize_t hrm_consumer_group_write(struct file *file, const char __user *buf, size_t size, loff_t *off)
{
	ssize_t retval;
	struct task_struct *task;
	char kbuf[1024];
	int gid;

	task = get_proc_task(file->f_dentry->d_inode);

	if (task != current) {
		printk(KERN_ERR __FILE__ " @ %d task %d not valid\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_valid;
	}

	memset(kbuf, 0, sizeof(char) * 1024);
	if (copy_from_user(kbuf, buf, size) != 0) {
		printk(KERN_ERR __FILE__ " @ %d copy_from_user() failed\n", __LINE__);
		retval = -EFAULT;
		goto failure_copy_from_user;
	}

	gid = (int) simple_strtol(kbuf, NULL, 10);
	if (gid > 0) {
		retval = hrm_add_consumer_task_to_group(task, gid);
		if (retval < 0) {
			printk(KERN_ERR __FILE__ " @ %d hrm_add_consumer_task_to_group() failed\n", __LINE__);
			goto failure_add_consumer_task_to_group;
		}
	} else if (gid < 0) {
		gid = -gid;
		retval = hrm_delete_consumer_task_from_group(task, gid);
		if (retval < 0) {
			printk(KERN_ERR __FILE__ " @ %d hrm_delete_consumer_task_from_group() failed\n", __LINE__);
			goto failure_delete_consumer_task_from_group;
		}
	} else {
		printk(KERN_ERR __FILE__ " @ %d group 0 not valid\n", __LINE__);
		retval = -EINVAL;
		goto failure_group_not_valid;
	}

	retval = (ssize_t) size;

failure_group_not_valid:
failure_add_consumer_task_to_group:
failure_delete_consumer_task_from_group:
failure_copy_from_user:
failure_task_not_valid:
	put_task_struct(task);

	return retval;
}

ssize_t hrm_producer_counter_read(struct file *file, char __user *buf, size_t size, loff_t *off)
{
	ssize_t retval;
	struct task_struct *task;
	struct hrm_producer *producer;
	char kbuf[1024];
	int ksize;

	task = get_proc_task(file->f_dentry->d_inode);

	if (task != current) {
		printk(KERN_ERR __FILE__ " @ %d task %d not valid\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_valid;
	}

	if (!hrm_producer_task_enabled(task)) {
		printk(KERN_ERR __FILE__ " @ %d task %d not enabled\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_enabled;
	}

	producer = list_first_entry(&task->hrm_producers, struct hrm_producer, task_link);
	ksize = snprintf(kbuf, 1024, "%lu", producer->counter_user_address);
	retval = simple_read_from_buffer(buf, size, off, kbuf, ksize);

failure_task_not_enabled:
failure_task_not_valid:
	put_task_struct(task);

	return retval;
}

int hrm_producer_counter_mmap(struct file* file, struct vm_area_struct *vma)
{
	int retval = 0;
	struct task_struct *task;
	struct hrm_producer *producer;
	struct hrm_memory *counters;
	unsigned long members_lock_flags;
	unsigned long user_address;
	long size;

	task = get_proc_task(file->f_dentry->d_inode);

	if (task != current) {
		printk(KERN_ERR __FILE__ " @ %d task %d not valid\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_valid;
	}

	if (!hrm_producer_task_enabled(task)) {
		printk(KERN_ERR __FILE__ " @ %d task %d not enabled\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_enabled;
	}

	producer = list_first_entry(&task->hrm_producers, struct hrm_producer, task_link);
	write_lock_irqsave(&producer->group->members_lock, members_lock_flags);
	counters = &producer->group->counters;
	user_address = __hrm_find_group_memory_map(task, counters);
	if (user_address != 0) {
		retval = __hrm_get_group_memory_map(task, counters);
		if (retval < 0) {
			printk(KERN_ERR __FILE__ " @ %d __hrm_get_group_memory_map() failed\n", __LINE__);
			goto failure_get_group_memory_map;
		}
		goto exit;
	}

	size = vma->vm_end - vma->vm_start;
	if (size != counters->size) {
		printk(KERN_ERR __FILE__ " @ %d size not valid\n", __LINE__);
		retval = -EINVAL;
		goto failure_size;
	}
	retval = remap_pfn_range(vma, vma->vm_start, virt_to_phys((void *) counters->kernel_address) >> PAGE_SHIFT, size, vma->vm_page_prot);
	if (retval < 0) {
		printk(KERN_ERR __FILE__ " @ %d remap_pfn_range() failed\n", __LINE__);
		goto failure_remap_pfn_range;
	}
	user_address = vma->vm_start;
	retval = __hrm_add_group_memory_map(task, counters, user_address);
	if (retval < 0) {
		printk(KERN_ERR __FILE__ " @ %d __hrm_add_group_counters_map() failed\n", __LINE__);
		goto failure_add_group_memory_map;
	}

exit:
	producer->counter_user_address = HRM_COUNTER_ADDR(user_address,
							producer->counter_index);
failure_add_group_memory_map:
failure_remap_pfn_range:
failure_size:
failure_get_group_memory_map:
	write_unlock_irqrestore(&producer->group->members_lock, members_lock_flags);
failure_task_not_enabled:
failure_task_not_valid:
	put_task_struct(task);

	return retval;
}

ssize_t hrm_consumer_counter_read(struct file *file, char __user *buf, size_t size, loff_t *off)
{
	ssize_t retval;
	struct task_struct *task;
	struct hrm_consumer *consumer;
	char kbuf[1024];
	int ksize;

	task = get_proc_task(file->f_dentry->d_inode);

	if (task != current) {
		printk(KERN_ERR __FILE__ " @ %d task %d not valid\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_valid;
	}

	if (!hrm_consumer_task_enabled(task)) {
		printk(KERN_ERR __FILE__ " @ %d task %d not enabled\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_enabled;
	}

	consumer = list_first_entry(&task->hrm_consumers, struct hrm_consumer, task_link);
	ksize = snprintf(kbuf, 1024, "%lu", consumer->counter_user_address);
	retval = simple_read_from_buffer(buf, size, off, kbuf, ksize);

failure_task_not_enabled:
failure_task_not_valid:
	put_task_struct(task);

	return retval;
}

int hrm_consumer_counter_mmap(struct file* file, struct vm_area_struct *vma)
{
	int retval = 0;
	struct task_struct *task;
	struct hrm_consumer *consumer;
	struct hrm_memory *counters;
	unsigned long members_lock_flags;
	unsigned long user_address;
	long size;

	task = get_proc_task(file->f_dentry->d_inode);

	if (task != current) {
		printk(KERN_ERR __FILE__ " @ %d task %d not valid\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_valid;
	}

	if (!hrm_consumer_task_enabled(task)) {
		printk(KERN_ERR __FILE__ " @ %d task %d not enabled\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_enabled;
	}

	consumer = list_first_entry(&task->hrm_consumers, struct hrm_consumer, task_link);
	write_lock_irqsave(&consumer->group->members_lock, members_lock_flags);
	counters = &consumer->group->counters;
	user_address = __hrm_find_group_memory_map(task, counters);
	if (user_address != 0) {
		retval = __hrm_get_group_memory_map(task, counters);
		if (retval < 0) {
			printk(KERN_ERR __FILE__ " @ %d __hrm_get_group_memory_map() failed\n", __LINE__);
			goto failure_get_group_memory_map;
		}
		goto exit;
	}

	size = vma->vm_end - vma->vm_start;
	if (size != counters->size) {
		printk(KERN_ERR __FILE__ " @ %d size not valid\n", __LINE__);
		retval = -EINVAL;
		goto failure_size;
	}
	retval = remap_pfn_range(vma, vma->vm_start, virt_to_phys((void *) counters->kernel_address) >> PAGE_SHIFT, size, vma->vm_page_prot);
	if (retval < 0) {
		printk(KERN_ERR __FILE__ " @ %d remap_pfn_range() failed\n", __LINE__);
		goto failure_remap_pfn_range;
	}
	user_address = vma->vm_start;
	retval = __hrm_add_group_memory_map(task, counters, user_address);
	if (retval < 0) {
		printk(KERN_ERR __FILE__ " @ %d __hrm_add_group_counters_map() failed\n", __LINE__);
		goto failure_add_group_memory_map;
	}

exit:
	consumer->counter_user_address = HRM_COUNTERS_ADDR(user_address);
failure_add_group_memory_map:
failure_remap_pfn_range:
failure_size:
failure_get_group_memory_map:
	write_unlock_irqrestore(&consumer->group->members_lock, members_lock_flags);
failure_task_not_enabled:
failure_task_not_valid:
	put_task_struct(task);

	return retval;
}

ssize_t hrm_producer_measures_goal_read(struct file *file, char __user *buf, size_t size, loff_t *off)
{
	ssize_t retval;
	struct task_struct *task;
	struct hrm_producer *producer;
	char kbuf[1024];
	int ksize;

	task = get_proc_task(file->f_dentry->d_inode);

	if (task != current) {
		printk(KERN_ERR __FILE__ " @ %d task %d not valid\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_valid;
	}

	if (!hrm_producer_task_enabled(task)) {
		printk(KERN_ERR __FILE__ " @ %d task %d not enabled\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_enabled;
	}

	producer = list_first_entry(&task->hrm_producers, struct hrm_producer, task_link);
	ksize = snprintf(kbuf, 1024, "%lu %lu", producer->measures_user_address, producer->goal_user_address);
	retval = simple_read_from_buffer(buf, size, off, kbuf, ksize);

failure_task_not_enabled:
failure_task_not_valid:
	put_task_struct(task);

	return retval;
}

int hrm_producer_measures_goal_mmap(struct file* file, struct vm_area_struct *vma)
{
	int retval = 0;
	struct task_struct *task;
	struct hrm_producer *producer;
	struct hrm_memory *measures_goal;
	unsigned long members_lock_flags;
	unsigned long user_address;
	long size;

	task = get_proc_task(file->f_dentry->d_inode);

	if (task != current) {
		printk(KERN_ERR __FILE__ " @ %d task %d not valid\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_valid;
	}

	if (!hrm_producer_task_enabled(task)) {
		printk(KERN_ERR __FILE__ " @ %d task %d not enabled\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_enabled;
	}

	producer = list_first_entry(&task->hrm_producers, struct hrm_producer, task_link);
	write_lock_irqsave(&producer->group->members_lock, members_lock_flags);
	measures_goal = &producer->group->measures_goal;
	user_address = __hrm_find_group_memory_map(task, measures_goal);
	if (user_address != 0) {
		retval = __hrm_get_group_memory_map(task, measures_goal);
		if (retval < 0) {
			printk(KERN_ERR __FILE__ " @ %d __hrm_get_group_memory_map() failed\n", __LINE__);
			goto failure_get_group_memory_map;
		}
		goto exit;
	}

	size = vma->vm_end - vma->vm_start;
	if (size != producer->group->measures_goal.size) {
		printk(KERN_ERR __FILE__ " @ %d size not valid\n", __LINE__);
		retval = -EINVAL;
		goto failure_size;
	}
	retval = remap_pfn_range(vma, vma->vm_start, virt_to_phys((void *) producer->group->measures_goal.kernel_address) >> PAGE_SHIFT, size, vma->vm_page_prot);
	if (retval < 0) {
		printk(KERN_ERR __FILE__ " @ %d remap_pfn_range() failed\n", __LINE__);
		goto failure_remap_pfn_range;
	}
	user_address = vma->vm_start;
	retval = __hrm_add_group_memory_map(task, measures_goal, user_address);
	if (retval < 0) {
		printk(KERN_ERR __FILE__ " @ %d __hrm_add_group_counters_map() failed\n", __LINE__);
		goto failure_add_group_memory_map;
	}

exit:
	producer->measures_user_address = HRM_MEASURES_ADDR(user_address);
	producer->goal_user_address = HRM_GOAL_ADDR(user_address);
failure_add_group_memory_map:
failure_remap_pfn_range:
failure_size:
failure_get_group_memory_map:
	write_unlock_irqrestore(&producer->group->members_lock, members_lock_flags);
failure_task_not_enabled:
failure_task_not_valid:
	put_task_struct(task);

	return retval;
}

ssize_t hrm_consumer_measures_goal_read(struct file *file, char __user *buf, size_t size, loff_t *off)
{
	ssize_t retval;
	struct task_struct *task;
	struct hrm_consumer *consumer;
	char kbuf[1024];
	int ksize;

	task = get_proc_task(file->f_dentry->d_inode);

	if (task != current) {
		printk(KERN_ERR __FILE__ " @ %d task %d not valid\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_valid;
	}

	if (!hrm_consumer_task_enabled(task)) {
		printk(KERN_ERR __FILE__ " @ %d task %d not enabled\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_enabled;
	}

	consumer = list_first_entry(&task->hrm_consumers, struct hrm_consumer, task_link);
	ksize = snprintf(kbuf, 1024, "%lu %lu", consumer->measures_user_address, consumer->goal_user_address);
	retval = simple_read_from_buffer(buf, size, off, kbuf, ksize);

failure_task_not_enabled:
failure_task_not_valid:
	put_task_struct(task);

	return retval;
}

int hrm_consumer_measures_goal_mmap(struct file* file, struct vm_area_struct *vma)
{
	int retval = 0;
	struct task_struct *task;
	struct hrm_consumer *consumer;
	struct hrm_memory *measures_goal;
	unsigned long members_lock_flags;
	unsigned long user_address;
	long size;

	task = get_proc_task(file->f_dentry->d_inode);

	if (task != current) {
		printk(KERN_ERR __FILE__ " @ %d task %d not valid\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_valid;
	}

	if (!hrm_consumer_task_enabled(task)) {
		printk(KERN_ERR __FILE__ " @ %d task %d not enabled\n", __LINE__, (int) task->pid);
		retval = -EPERM;
		goto failure_task_not_enabled;
	}

	consumer = list_first_entry(&task->hrm_consumers, struct hrm_consumer, task_link);
	write_lock_irqsave(&consumer->group->members_lock, members_lock_flags);
	measures_goal = &consumer->group->measures_goal;
	user_address = __hrm_find_group_memory_map(task, measures_goal);
	if (user_address != 0) {
		retval = __hrm_get_group_memory_map(task, measures_goal);
		if (retval < 0) {
			printk(KERN_ERR __FILE__ " @ %d __hrm_get_group_memory_map() failed\n", __LINE__);
			goto failure_get_group_memory_map;
		}
		goto exit;
	}

	size = vma->vm_end - vma->vm_start;
	if (size != measures_goal->size) {
		printk(KERN_ERR __FILE__ " @ %d size not valid\n", __LINE__);
		retval = -EINVAL;
		goto failure_size;
	}
	retval = remap_pfn_range(vma, vma->vm_start, virt_to_phys((void *) measures_goal->kernel_address) >> PAGE_SHIFT, size, vma->vm_page_prot);
	if (retval < 0) {
		printk(KERN_ERR __FILE__ " @ %d remap_pfn_range() failed\n", __LINE__);
		goto failure_remap_pfn_range;
	}
	user_address = vma->vm_start;
	retval = __hrm_add_group_memory_map(task, measures_goal, user_address);
	if (retval < 0) {
		printk(KERN_ERR __FILE__ " @ %d __hrm_add_group_counters_map() failed\n", __LINE__);
		goto failure_add_group_memory_map;
	}

exit:
	consumer->measures_user_address = HRM_MEASURES_ADDR(user_address);
	consumer->goal_user_address = HRM_GOAL_ADDR(user_address);
failure_add_group_memory_map:
failure_remap_pfn_range:
failure_size:
failure_get_group_memory_map:
	write_unlock_irqrestore(&consumer->group->members_lock, members_lock_flags);
failure_task_not_enabled:
failure_task_not_valid:
	put_task_struct(task);

	return retval;
}

static int hrm_show(struct seq_file *seq_file, void *v)
{
	struct hrm_group *group;
	unsigned long members_lock_flags;
	struct hrm_producer *producer;
	struct hrm_measures *measures;
	struct hrm_goal *goal;
	int groups_number = 0;
	int i;
	struct timespec ts;
	u64 min, max, hr;
	size_t scope;

	spin_lock(&hrm_groups_lock);

	getrawmonotonic(&ts);

	list_for_each_entry(group, &hrm_groups, link) {
		groups_number++;
	}
	seq_printf(seq_file, "%d monitored groups found:", groups_number);
	if (!groups_number)
		seq_printf(seq_file, "\n");

	list_for_each_entry(group, &hrm_groups, link) {
		seq_printf(seq_file, "\n\ngid: %d\ntids:", group->gid);
		read_lock_irqsave(&group->members_lock, members_lock_flags);
		list_for_each_entry (producer, &group->producers, group_link) {
			seq_printf(seq_file, " %d", (int) producer->counter->tid);
		}
		read_unlock_irqrestore(&group->members_lock, members_lock_flags);
		measures = (struct hrm_measures *)
			HRM_MEASURES_ADDR(group->measures_goal.kernel_address);
		goal = (struct hrm_goal *)
			HRM_GOAL_ADDR(group->measures_goal.kernel_address);
		hr = hrm_get_heart_rate(group, &scope, 0);
		min = hrm_get_min_heart_rate(group, &scope);
		max = hrm_get_max_heart_rate(group, &scope);
		seq_printf(seq_file, "\n"
				"minimum heart rate: %llu [hb/%d / s]\n"
				"maximum heart rate: %llu [hb/%d / s]\n"
				"goal scope: %zu\n"
				"\tglobal heart rate: %llu [hb/%d / s]\n",
				(unsigned long long) min, HRM_MEASURE_SCALE,
				(unsigned long long) max, HRM_MEASURE_SCALE,
				scope,
				(unsigned long) hr, HRM_MEASURE_SCALE);

		for (i = 1; i <= HRM_MAX_WINDOWS; i++) {
			hr = hrm_get_heart_rate(group, &scope, i);
			if (scope != HRM_MAX_WINDOW_SIZE)
				seq_printf(seq_file, "window %d\n"
					"\twindow heart rate: %llu [hb/%d / s]\n"
					"\twindow size: %u\n",
					i,
					(unsigned long long) hr, HRM_MEASURE_SCALE,
					(unsigned int) scope);
		}
	}
	spin_unlock(&hrm_groups_lock);
	return 0;
}

static int hrm_open(struct inode *inode, struct file *file)
{
	return single_open(file, hrm_show, NULL);
}

static const struct file_operations proc_hrm_operations = {
	.llseek = seq_lseek,
	.read = seq_read,
	.open = hrm_open,
	.release = single_release,
};

static int __init proc_hrm_init(void)
{
	proc_create("hrm", 0444, NULL, &proc_hrm_operations);
	return 0;
}
module_init(proc_hrm_init);

