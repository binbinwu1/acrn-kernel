#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define COUNTER_INITIAL_VAL 		0x800000000001ULL
#define TYPE_FIXED_CTR  		(1ULL << 30)

#define IC_PMU_START	0x1
#define IC_PMU_STOP	0x2


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

struct pmu_events {
	uint64_t count;
	struct pmu_event events[8];
};


struct pmu_events *events;

static int file_desc;

static uint64_t fixed_event_s_values[3];
static uint64_t gp_event_s_values[8];
static uint64_t fixed_event_e_values[3];
static uint64_t gp_event_e_values[8];

static struct pmu_events level1_events = {
	.count = 4,
	.events = {
		//IDQ_UOPS_NOT_DELIVERED.CORE
		{
			.event = 0x9c,
			.umask = 0x01,
			.os = 1,
			.user = 1,
		},
		//UOPS_ISSUED.ANY
		{
			.event = 0x0e,
			.umask = 0x01,
			.os = 1,
			.user = 1,
		},
		//UOPS_RETIRED.RETIRE_SLOTS
		{
			.event = 0xc2,
			.umask = 0x02,
			.os = 1,
			.user = 1,
		},
		//INT_MISC.RECOVERY_CYCLES
		{
			.event = 0x0d,
			.umask = 0x01,
			.os = 1,
			.user = 1,
		}
	}

};

static struct pmu_events level2_events = {
	.count = 8,
	.events = {
		//CYCLE_ACTIVITY.STALLS_MEM_ANY
		{
			.event = 0xa3,
			.umask = 0x14,
			.cmask = 20,
			.os = 1,
			.user = 1,
		},
		//EXE_ACTIVITY.BOUND_ON_STORES,
		{
			.event = 0xa6,
			.umask = 0x40,
			.os = 1,
			.user = 1,
		},
		//EXE_ACTIVITY.EXE_BOUND_0_PORTS,
		{
			.event = 0xa6,
			.umask = 0x01,
			.os = 1,
			.user = 1,
		},
		//EXE_ACTIVITY.1_PORTS_UTIL,
		{
			.event = 0xa6,
			.umask = 0x02,
			.os = 1,
			.user = 1,
		},
		//IDQ_UOPS_NOT_DELIVERED.CORE
		{
			.event = 0x9c,
			.umask = 0x01,
			.os = 1,
			.user = 1,
		},
		//UOPS_ISSUED.ANY
		{
			.event = 0x0e,
			.umask = 0x01,
			.os = 1,
			.user = 1,
		},
		//UOPS_RETIRED.RETIRE_SLOTS
		{
			.event = 0xc2,
			.umask = 0x02,
			.os = 1,
			.user = 1,
		},
		//INT_MISC.RECOVERY_CYCLES
		{
			.event = 0x0d,
			.umask = 0x01,
			.os = 1,
			.user = 1,
		},
	}
};

static struct pmu_events level3_mem_events = {
	.count = 8,
	.events = {
		// CYCLE_ACTIVITY.STALLS_MEM_ANY,
		{
			.event = 0xa3,
			.umask = 0x14,
			.cmask = 20,
			.os = 1,
			.user = 1,
		},
		// CYCLE_ACTIVITY.STALLS_L1D_MISS,
		{
			.event = 0xa3,
			.umask = 0x08,
			.cmask = 8,
			.os = 1,
			.user = 1,
		},
		// CYCLE_ACTIVITY.STALLS_L2_MISS,
		{
			.event = 0xa3,
			.umask = 0x05,
			.cmask = 5,
			.os = 1,
			.user = 1,
		},
		// CYCLE_ACTIVITY.STALLS_L3_MISS,
		{
			.event = 0xa3,
			.umask = 0x06,
			.cmask = 6,
			.os = 1,
			.user = 1,
		},
		// EXE_ACTIVITY.BOUND_ON_STORES,
		{
			.event = 0xa6,
			.umask = 0x40,
			.os = 1,
			.user = 1,
		},
		// DTLB_LOAD_MISSES.STLB_HIT,
		{
			.event = 0x08,
			.umask = 0x20,
			.os = 1,
			.user = 1,
		},
		// DTLB_LOAD_MISSES.WALK_ACTIVE,
		{
			.event = 0x08,
			.umask = 0x10,
			.cmask = 1,
			.os = 1,
			.user = 1,
		},
		// LD_BLOCKS.STORE_FORWARD,
		{
			.event = 0x03,
			.umask = 0x02,
			.os = 1,
			.user = 1,
		},
	}
};


static struct pmu_events level4_l1bound_0_events = {
	.count = 6,
	.events = {
		//L1D_PEND_MISS.PENDING,
		{
			.event = 0x48,
			.umask = 0x01,
			.os = 1,
			.user = 1,
		},
		//MEM_LOAD_RETIRED.L1_MISS,
		{
			.event = 0xd1,
			.umask = 0x08,
			.os = 1,
			.user = 1,
		},
		//MEM_LOAD_RETIRED.FB_HIT,
		{
			.event = 0xd1,
			.umask = 0x40,
			.os = 1,
			.user = 1,
		},
		//L1D_PEND_MISS.FB_FULL,
		{
			.event = 0x48,
			.umask = 0x02,
			.os = 1,
			.user = 1,
		},
		//LD_BLOCKS.NO_SR,
		{
			.event = 0x03,
			.umask = 0x08,
			.os = 1,
			.user = 1,
		},
		//LD_BLOCKS_PARTIAL.ADDRESS_ALIA
		{
			.event = 0x07,
			.umask = 0x01,
			.os = 1,
			.user = 1,
		},
	}
};

static struct pmu_events level4_l1bound_1_events  = {
	.count = 4,
	.events = {
		//MEM_INST_RETIRED_LOCK_LOADS,
		{
			.event = 0xd0,
			.umask = 0x21,
			.os = 1,
			.user = 1,
		},
		//MEM_INST_RETIRED_ALL_STORES,
		{
			.event = 0xd0,
			.umask = 0x82,
			.os = 1,
			.user = 1,
		},
		//CPU_CLK_UNHALTED_THREAD,
		{
			.event = 0x3c,
			.umask = 0x00,
			.os = 1,
			.user = 1,
		},
		//OFFCORE_REQUESTS_OUTSTANDING_CYCLES_WITH_DEMAND_RFO,
		{
			.event = 0x60,
			.umask = 0x04,
			.cmask = 1,
			.os = 1,
			.user = 1,
		},
	}
};


int pmu_start(uint64_t level)
{
	printf("PMU ENABLED\n");
	file_desc = open("/dev/acrn_pmu", 0);

	if (file_desc < 0) {
		printf("Can't open device file: /dev/acrn_pmu\n");
		exit(-1);
	}

	if (level == 1){
		events = &level1_events;
	} else if (level == 2){
		events = &level2_events;
	} else if (level == 3) {
		events = &level3_mem_events;
	} else if (level == 4) {
		events = &level4_l1bound_0_events;
	} else if (level == 5) {
		events = &level4_l1bound_1_events;
	}  else {
		printf("Undefined level %lu\n", level);
		exit(-1);
	}

	ioctl(file_desc, IC_PMU_START, events);
}

int pmu_stop(void)
{
	ioctl(file_desc, IC_PMU_STOP, 0);

	close(file_desc);
}

static inline uint64_t _x86_pmc_read(unsigned int idx)
{
    uint32_t lo, hi;

    __asm__ volatile("rdpmc" : "=a" (lo), "=d" (hi) : "c" (idx));

    return ((uint64_t)lo) | (((uint64_t)hi) << 32);
}

void pmu_read_start(void)
{
	uint64_t i;

	for (i = 0; i < 3; i++) {
		fixed_event_s_values[i] = _x86_pmc_read(i | TYPE_FIXED_CTR);
	}

	for (i = 0; i < 8; i++) {
		gp_event_s_values[i] = _x86_pmc_read(i);
	}
}

void pmu_read_end(void)
{
	uint64_t i;

	for (i = 0; i < 3; i++) {
		fixed_event_e_values[i] = _x86_pmc_read(i | TYPE_FIXED_CTR);
	}

	for (i = 0; i < 8; i++) {
		gp_event_e_values[i] = _x86_pmc_read(i);
	}
}

void pmu_print(uint64_t tsc_diff)
{
	printf("%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
		tsc_diff,
		fixed_event_e_values[0] - fixed_event_s_values[0],
		fixed_event_e_values[1] - fixed_event_s_values[1],
		fixed_event_e_values[2] - fixed_event_s_values[2],
		gp_event_e_values[0] - gp_event_s_values[0],
		gp_event_e_values[1] - gp_event_s_values[1],
		gp_event_e_values[2] - gp_event_s_values[2],
		gp_event_e_values[3] - gp_event_s_values[3],
		gp_event_e_values[4] - gp_event_s_values[4],
		gp_event_e_values[5] - gp_event_s_values[5],
		gp_event_e_values[6] - gp_event_s_values[6],
		gp_event_e_values[7] - gp_event_s_values[7]);
}