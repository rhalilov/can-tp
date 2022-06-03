/*
 * cbtimer_lin.c
 *
 *  Created on: May 20, 2022
 *      Author: refo
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>

#include "cbtimer_lin.h"

#if CBTIMER_LOG
	#define cbtimer_log printf
	#define cbtimer_flush fflush
#else
	#define cbtimer_log
	#define cbtimer_flush
#endif

void cbtimer_cb(union sigval sev)
{
	cbtimer_t *tim = sev.sival_ptr;
	if (atomic_load(&tim->status) == CBTIMER_RUN) {
		atomic_store(&tim->status, CBTIMER_STOP);
		sem_post(&tim->sem_expired);
		cbtimer_log ("Timer expired %s "
//				"(0x%08x) "
				"%ld\n",
						tim->name,
//						tim,
						(long int)tim->timerId);
		tim->cb(tim);
	}
}

int cbtimer_set_cb(cbtimer_t *tim, void (*cb)(struct cbtimer_s *timer),
														void *cb_params)
{
	struct sigevent sev;
	sev.sigev_notify = SIGEV_THREAD;
	sev.sigev_value.sival_ptr = tim;
	sev.sigev_notify_function = cbtimer_cb;
	sev.sigev_notify_attributes = 0;
	tim->cb = cb;
	tim->cb_params = cb_params;
	tim->name = NULL;
	sem_init(&tim->sem_expired, 1, 0);
	if (timer_create(CLOCK_REALTIME, &sev, &tim->timerId)) {
		cbtimer_log("Error: timer_create\n"); cbtimer_flush(0);
		return -1;
	}
	return 0;
}

void cbtimer_set_name(cbtimer_t *tim, char *name)
{
//	cbtimer_log("Timer name: %s\n", name);cbtimer_flush(0);
	tim->name = name;
}

int cbtimer_start(cbtimer_t *tim, long us)
{
	int r;
	struct itimerspec its;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	its.it_value.tv_sec = us / 1000000;
	its.it_value.tv_nsec = us % 1000000;
	cbtimer_log ("Starting timer %s "
//							"(0x%08x) "
							"%ld for "
							"%ldμs\n",
							tim->name,
//							tim,
							(long int)tim->timerId,
							us);
	cbtimer_flush(0);
	atomic_store(&tim->status, CBTIMER_RUN);
	r = timer_settime(tim->timerId, 0, &its, NULL);
	if (r) {
		cbtimer_log("Error: timer_settime\n"); cbtimer_flush(0);
		return -1;
	}
	return 0;
}

void cbtimer_stop(cbtimer_t *tim)
{
	if (atomic_load(&tim->status) == CBTIMER_RUN) {
		atomic_store(&tim->status, CBTIMER_STOP);
		cbtimer_log("Stopping timer %s "
//				"(0x%0x) "
				"%ld\n",
				tim->name,
//				tim,
				(long int)tim->timerId); cbtimer_flush(0);
	}
}

int cbtimer_is_expired(cbtimer_t *tim)
{
	int ovr = timer_getoverrun(&tim->status);
	cbtimer_log("timer_getoverrun = %d\n", ovr); cbtimer_flush(0);
	return (ovr > 0);
}

void cbtimer_wait_to_expire(cbtimer_t *tim)
{
	sem_wait(&tim->sem_expired);
}
