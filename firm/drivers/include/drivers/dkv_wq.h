/*
 * Shared workqueue for the drivers in this module (keeps their deferred work
 * off the system workqueue, which the Bluetooth host relies on).
 */
#ifndef DRIVERS_DKV_WQ_H_
#define DRIVERS_DKV_WQ_H_

#include <zephyr/kernel.h>

extern struct k_work_q dkv_wq;

#endif /* DRIVERS_DKV_WQ_H_ */
