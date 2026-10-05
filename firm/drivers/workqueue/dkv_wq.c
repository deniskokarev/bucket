#include <drivers/dkv_wq.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>

struct k_work_q dkv_wq;

static K_THREAD_STACK_DEFINE(dkv_wq_stack, CONFIG_DKV_WORKQUEUE_STACK_SIZE);

static int dkv_wq_init(void)
{
	const struct k_work_queue_config cfg = {.name = "dkv_wq"};

	k_work_queue_start(&dkv_wq, dkv_wq_stack, K_THREAD_STACK_SIZEOF(dkv_wq_stack),
			   K_PRIO_PREEMPT(CONFIG_DKV_WORKQUEUE_PRIORITY), &cfg);
	return 0;
}

/* Before the drivers that queue work here (KERNEL_INIT_PRIORITY_DEVICE). */
SYS_INIT(dkv_wq_init, POST_KERNEL, 0);
