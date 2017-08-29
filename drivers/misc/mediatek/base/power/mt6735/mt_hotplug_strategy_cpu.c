/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>

#include "mt_hotplug_strategy_internal.h"
#define HP_HAVE_SCHED_TPLG		0

#if !HP_HAVE_SCHED_TPLG
#include <linux/cpumask.h>
#include <asm/topology.h>
#endif

/*
 * hps cpu interface - cpumask
 */
int hps_cpu_is_cpu_big(int cpu)
{
	if (!cpumask_empty(&hps_ctxt.big_cpumask)) {
		if (cpumask_test_cpu(cpu, &hps_ctxt.big_cpumask))
			return 1;
		else
			return 0;
	} else
		return 0;
}

int hps_cpu_is_cpu_little(int cpu)
{
	if (!cpumask_empty(&hps_ctxt.little_cpumask)) {
		if (cpumask_test_cpu(cpu, &hps_ctxt.little_cpumask))
			return 1;
		else
			return 0;
	} else
		return 0;
}

unsigned int num_online_little_cpus(void)
{
	struct cpumask dst_cpumask;

	cpumask_and(&dst_cpumask, &hps_ctxt.little_cpumask, cpu_online_mask);
	return cpumask_weight(&dst_cpumask);
}

unsigned int num_online_big_cpus(void)
{
	struct cpumask dst_cpumask;

	cpumask_and(&dst_cpumask, &hps_ctxt.big_cpumask, cpu_online_mask);
	return cpumask_weight(&dst_cpumask);
}

/*
 * hps cpu interface - scheduler
 */
unsigned int hps_cpu_get_percpu_load(int cpu)
{
#ifdef CONFIG_MTK_SCHED_RQAVG_US
	return sched_get_percpu_load(cpu, 1, 0);
#else
	return 100;
#endif
}

unsigned int hps_cpu_get_nr_heavy_task(void)
{
#ifdef CONFIG_MTK_SCHED_RQAVG_US
	return sched_get_nr_heavy_task();
#else
	return 0;
#endif
}

void hps_cpu_get_tlp(unsigned int *avg, unsigned int *iowait_avg)
{
#ifdef CONFIG_MTK_SCHED_RQAVG_KS
	sched_get_nr_running_avg((int *)avg, (int *)iowait_avg);
#else
	*avg = 0;
	*iowait_avg = 0;
#endif
}

void hps_cpu_get_big_little_cpumasks(struct cpumask *big, struct cpumask *little)
{
#if HP_HAVE_SCHED_TPLG
	sched_get_big_little_cpus(big, little);
#else
	unsigned int cpu;

	cpumask_clear(big);
	cpumask_clear(little);

	for_each_possible_cpu(cpu) {
		if (arch_cpu_is_big(cpu))
			cpumask_set_cpu(cpu, big);
		else
			cpumask_set_cpu(cpu, little);
	}
#endif /* HP_HAVE_SCHED_TPLG */
}
/*
 * init
 */
int hps_cpu_init(void)
{
	int r = 0;
	char str1[32];

	hps_warn("hps_cpu_init\n");

	/* init cpu arch in hps_ctxt */
	/* init cpumask */
	cpumask_clear(&hps_ctxt.little_cpumask);
	cpumask_clear(&hps_ctxt.big_cpumask);

	/* a. call api */
	hps_cpu_get_big_little_cpumasks(&hps_ctxt.big_cpumask, &hps_ctxt.little_cpumask);
	/* b. fix 2L2b */
	/* cpulist_parse("0-1", &hps_ctxt.little_cpumask); */
	/* cpulist_parse("2-3", &hps_ctxt.big_cpumask); */
	/* c. 4L */
	/* cpulist_parse("0-3", &hps_ctxt.little_cpumask); */

	cpulist_scnprintf(str1, sizeof(str1), &hps_ctxt.little_cpumask);
	hps_warn("hps_ctxt.little_cpumask: %s\n", str1);
	cpulist_scnprintf(str1, sizeof(str1), &hps_ctxt.big_cpumask);
	hps_warn("hps_ctxt.big_cpumask: %s\n", str1);
	if (cpumask_weight(&hps_ctxt.little_cpumask) == 0) {
		cpumask_copy(&hps_ctxt.little_cpumask, &hps_ctxt.big_cpumask);
		cpumask_clear(&hps_ctxt.big_cpumask);
	}

	/* verify arch is hmp or smp */
	if (!cpumask_empty(&hps_ctxt.little_cpumask) &&
		!cpumask_empty(&hps_ctxt.big_cpumask)) {
		unsigned int cpu;

		hps_ctxt.is_hmp = 1;
		hps_ctxt.little_cpu_id_min = num_possible_cpus();
		hps_ctxt.big_cpu_id_min = num_possible_cpus();

		for_each_cpu((cpu), &hps_ctxt.little_cpumask) {
			if (cpu < hps_ctxt.little_cpu_id_min)
				hps_ctxt.little_cpu_id_min = cpu;
			if (cpu > hps_ctxt.little_cpu_id_max)
				hps_ctxt.little_cpu_id_max = cpu;
		}

		for_each_cpu((cpu), &hps_ctxt.big_cpumask) {
			if (cpu < hps_ctxt.big_cpu_id_min)
				hps_ctxt.big_cpu_id_min = cpu;
			if (cpu > hps_ctxt.big_cpu_id_max)
				hps_ctxt.big_cpu_id_max = cpu;
		}
	} else {
		hps_ctxt.is_hmp = 0;
		hps_ctxt.little_cpu_id_min = 0;
		hps_ctxt.little_cpu_id_max = num_possible_little_cpus() - 1;
	}

	/* init bound in hps_ctxt */
	hps_ctxt.little_num_base_perf_serv = 1;
	hps_ctxt.little_num_limit_thermal =
		cpumask_weight(&hps_ctxt.little_cpumask);
	hps_ctxt.little_num_limit_low_battery =
		cpumask_weight(&hps_ctxt.little_cpumask);
	hps_ctxt.little_num_limit_ultra_power_saving =
		cpumask_weight(&hps_ctxt.little_cpumask);
	hps_ctxt.little_num_limit_power_serv =
		cpumask_weight(&hps_ctxt.little_cpumask);
	hps_ctxt.big_num_base_perf_serv = 0;
	hps_ctxt.big_num_limit_thermal = cpumask_weight(&hps_ctxt.big_cpumask);
	hps_ctxt.big_num_limit_low_battery =
		cpumask_weight(&hps_ctxt.big_cpumask);
	hps_ctxt.big_num_limit_ultra_power_saving =
		cpumask_weight(&hps_ctxt.big_cpumask);
	hps_ctxt.big_num_limit_power_serv =
		cpumask_weight(&hps_ctxt.big_cpumask);

	hps_warn(
		"%s: little_cpu_id_min: %u, little_cpu_id_max: %u, big_cpu_id_min: %u, big_cpu_id_max: %u\n",
		__func__,
		hps_ctxt.little_cpu_id_min, hps_ctxt.little_cpu_id_max,
		hps_ctxt.big_cpu_id_min, hps_ctxt.big_cpu_id_max);

	return r;
}

/*
 * deinit
 */
int hps_cpu_deinit(void)
{
	int r = 0;

	hps_warn("hps_cpu_deinit\n");

	return r;
}