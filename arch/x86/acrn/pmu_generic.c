/*
 * Copyright (c) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/fs.h>

#define DEVICE_NAME	"acrn_pmu"
#define CLASS_NAME	"pmu"

#define IC_PMU_START	0x1
#define IC_PMU_STOP	0x2

#define COUNTER_INITIAL_VAL 		0x800000000001ULL
#define TYPE_FIXED_CTR  		(1ULL << 30)

#define PERF_GLOBAL_CTRL		0x38f /* Global CTRL MSR */
#define PERF_FIXED_CTR0			0x309 /* Fixed counter MSR for retired instructions */
#define PERF_FIXED_CTR1			0x30a /* Fixed counter MSR for cpu cycles */
#define PERF_FIXED_CTR2			0x30b /* Fixed counter MSR for cpu ref cycles */
#define PERF_FIXED_CTR_CTRL		0x38d /* Fixed counter control MSR */

#define PERF_GP_CTR_START_ADDR		0x4c1 /* General purpose counter MSR start address */
#define PERF_GP_EVTSEL_START_ADDR 	0x186 /* General purpose eventsel MSR start address */

#define PERFMON_EVENTSEL_ENABLE		(1ULL << 22)
#define PERFMON_EVENTSEL_USR           	(1ULL << 16)
#define PERFMON_EVENTSEL_OS            	(1ULL << 17)

#define RETIRED_INSTRUCTION_IDX  	0
#define CPU_CYCLES_IDX           	1
#define CPU_REF_CYCLES_IDX       	2

struct pmu_event {
	uint8_t event;
	uint8_t umask;
	uint8_t cmask;
	uint8_t any;
	uint8_t inv;
	uint8_t edg;
	uint8_t user;
	uint8_t os;
};

struct pmu_start_param {
	uint64_t count;
	struct pmu_event events[8];
};

static int major;
static struct class *pmu_class;
static struct device *pmu_device;
static int device_open = 0;
static struct pmu_start_param k_pmu_events_param;
static struct pmu_event *pmu_events;
static uint64_t pmu_event_count = 0;

static void start_fixed_perf_counter(int counter_idx, int counter_msr)
{
	uint64_t bits, mask, val;

	/* set initial value to counter MSR */
	wrmsrl_safe(counter_msr, COUNTER_INITIAL_VAL);

	/* config eventsel MSR */
	bits = 0x8ULL;

	if (pmu_events[counter_idx].user)
		bits |= 0x2;
	if (pmu_events[counter_idx].os)
		bits |= 0x1;

	bits <<= (counter_idx * 4);

	mask = 0xfULL << (counter_idx * 4);

	rdmsrl_safe(PERF_FIXED_CTR_CTRL, &val);
	val &= ~mask;
	val |= bits;
	wrmsrl_safe(PERF_FIXED_CTR_CTRL, val);
}

static void stop_fixed_perf_counter(int counter_idx)
{
	/* reset eventsel MSR */
	uint64_t val, mask;

	mask = 0xfULL << (counter_idx * 4);
	rdmsrl_safe(PERF_FIXED_CTR_CTRL, &val);
	val &= ~mask;
	wrmsrl_safe(PERF_FIXED_CTR_CTRL, val);
}

static void start_gp_perf_counter(int idx, struct pmu_event *evt)
{
	uint64_t val;

	/* set initial value to counter MSR */
	wrmsrl_safe(PERF_GP_CTR_START_ADDR + idx, COUNTER_INITIAL_VAL);

	/* config event select MSR */
	val = 0;
	if (pmu_events[idx].user)
		val |= PERFMON_EVENTSEL_USR;
	if (pmu_events[idx].os)
		val |= PERFMON_EVENTSEL_OS;
	val |= PERFMON_EVENTSEL_ENABLE;
	val |= evt->event;
	val |= evt->umask << 8;
	val |= evt->cmask << 24;
	wrmsrl_safe(PERF_GP_EVTSEL_START_ADDR + idx, val);
}

static void stop_gp_perf_counter(int idx)
{
	uint64_t val;

	/* disable counting by writing event select MSR */
	rdmsrl_safe(PERF_GP_EVTSEL_START_ADDR + idx, &val);
	val &= ~PERFMON_EVENTSEL_ENABLE;
	wrmsrl_safe(PERF_GP_EVTSEL_START_ADDR + idx, val);
}

static void disable_all_counter(void *info)
{
	wrmsrl_safe(PERF_GLOBAL_CTRL, 0);
}

static void enable_rdpmc_user(void)
{
	__asm__("push   %rax\n\t"
                "mov    %cr4,%rax;\n\t"
                "or     $(1 << 8),%rax;\n\t"
                "mov    %rax,%cr4;\n\t"
                //"wbinvd\n\t"
                "pop    %rax"
	);
}

static void disable_rdpmc_user(void)
{
	__asm__("push   %rax\n\t"
        	"push   %rbx\n\t"
                "mov    %cr4,%rax;\n\t"
                "mov  $(1 << 8), %rbx\n\t"
                "not  %rbx\n\t"
                "and   %rbx, %rax;\n\t"
                "mov    %rax,%cr4;\n\t"
                //"wbinvd\n\t"
                "pop    %rbx\n\t"
                "pop    %rax\n\t"
    );
}

void pmu_start(uint64_t ioctl_param)
{
	uint64_t i;
	struct pmu_start_param __user *param = (struct pmu_start_param __user *)ioctl_param;

	if (copy_from_user(&k_pmu_events_param, param, sizeof(struct pmu_start_param))) {
		pr_err("ERROR copying ioctl args from userspace\n");
		return;
	}

	pmu_events = k_pmu_events_param.events;
	pmu_event_count = k_pmu_events_param.count;

	pr_info("%s: pmu event count = %llu", __func__, pmu_event_count);

	/* for cpu retired instructions */
	start_fixed_perf_counter(RETIRED_INSTRUCTION_IDX, PERF_FIXED_CTR0);
	/* for cpu cycles */
	start_fixed_perf_counter(CPU_CYCLES_IDX, PERF_FIXED_CTR1);
	/* for cpu ref cycles */
	start_fixed_perf_counter(CPU_REF_CYCLES_IDX, PERF_FIXED_CTR2);

	for (i = 0; i < pmu_event_count; i++) {
		pr_info("%s: set up pmu event 0x%x umask 0x%x cmask 0x%x os %u user %u",
			__func__, pmu_events[i].event, pmu_events[i].umask, pmu_events[i].cmask,
			pmu_events[i].os, pmu_events[i].user);
		start_gp_perf_counter(i, &pmu_events[i]);
	}

	/* enable all counter */
	wrmsrl_safe(PERF_GLOBAL_CTRL, (0x7ULL << 32) | 0xff);

	enable_rdpmc_user();
}
EXPORT_SYMBOL(pmu_start);

void pmu_stop(void)
{
	uint64_t i;

	/* disable all counter */
	wrmsrl_safe(PERF_GLOBAL_CTRL, 0);
	/* for cpu retired instructions */
	stop_fixed_perf_counter(RETIRED_INSTRUCTION_IDX);
	/* for cpu cycles */
	stop_fixed_perf_counter(CPU_CYCLES_IDX);
	/* for cpu ref cycles */
	stop_fixed_perf_counter(CPU_REF_CYCLES_IDX);

	for (i = 0; i < pmu_event_count; i++) {
		stop_gp_perf_counter(i);
	}

	disable_rdpmc_user();

}
EXPORT_SYMBOL(pmu_stop);

static long pmu_dev_ioctl(struct file *filep,
		unsigned int ioctl_num, unsigned long ioctl_param)
{
	if (ioctl_num == IC_PMU_START) {
		pmu_start(ioctl_param);
	} else if (ioctl_num == IC_PMU_STOP) {
		pmu_stop();
	} else {
		pr_info("%s: invalide ioctl_num", __func__);
	}

	return 0;
}

static int pmu_dev_open(struct inode *inodep, struct file *filep)
{

	pr_info("%s\n", __func__);
	if (device_open)
		return -EBUSY;
	device_open++;

	try_module_get(THIS_MODULE);
	return 0;
}

static int pmu_dev_release(struct inode *inodep, struct file *filep)
{
	pr_info("%s\n", __func__);
	device_open--;

	module_put(THIS_MODULE);

	return 0;
}

static ssize_t pmu_dev_read(struct file *filep, char *buffer, size_t len,
		loff_t *offset)
{
	/* Does Nothing */
	return 0;
}

static ssize_t pmu_dev_write(struct file *filep, const char *buffer,
		size_t len, loff_t *offset)
{
	/* Does Nothing */
	return 0;
}

struct file_operations Fops = {
	.read = pmu_dev_read,
	.write = pmu_dev_write,
	.unlocked_ioctl = pmu_dev_ioctl,
	.open = pmu_dev_open,
	.release = pmu_dev_release,	/* a.k.a. close */
};

static __exit void acrn_pmu_module_exit(void)
{
	on_each_cpu(disable_all_counter, NULL, 0);
	pr_info("Exited PMU basic module\n");
}

int acrn_pmu_module_init(void)
{
	major = register_chrdev(0, DEVICE_NAME, &Fops);
	/*
	 * Negative values signify an error
	 */
	if (major < 0) {
		printk(KERN_ALERT "%s failed with %d\n",
		       "Sorry, registering the character device ", major);
	}

	/* Register the device class */
	pmu_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(pmu_class)) {
		unregister_chrdev(major, DEVICE_NAME);
		pr_warn("pmu: failed to register device class\n");
		return PTR_ERR(pmu_class);
	}
	pr_info("pmu: device class registered correctly\n");

	/* Register the device driver */
	pmu_device = device_create(pmu_class, NULL, MKDEV(major, 0),
		NULL, DEVICE_NAME);
	if (IS_ERR(pmu_device)) {
		class_destroy(pmu_class);
		unregister_chrdev(major, DEVICE_NAME);
		pr_warn("pmu: failed to create the device\n");
		return PTR_ERR(pmu_device);
	}

	return 0;
}

module_init(acrn_pmu_module_init);
module_exit(acrn_pmu_module_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("binbin.wu@intel.com");
MODULE_DESCRIPTION("Basic PMU TMAM library module");