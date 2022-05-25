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

void cbtimer_cb(union sigval sev)
{
	cbtimer_t *tim = sev.sival_ptr;
	if (atomic_load(&tim->status) == CBTIMER_RUN) {
		atomic_store(&tim->status, CBTIMER_STOP);
		printf ("Timer expired %s "
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
	if (timer_create(CLOCK_REALTIME, &sev, &tim->timerId)) {
		printf("Error: timer_create\n"); fflush(0);
		return -1;
	}
	return 0;
}

void cbtimer_set_name(cbtimer_t *tim, char *name)
{
//	printf("Timer name: %s\n", name);fflush(0);
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
	printf ("Starting timer %s "
//							"(0x%08x) "
							"%ld for "
							"%ldμs\n",
							tim->name,
//							tim,
							(long int)tim->timerId,
							us);
	fflush(0);
	atomic_store(&tim->status, CBTIMER_RUN);
	r = timer_settime(tim->timerId, 0, &its, NULL);
	if (r) {
		printf("Error: timer_settime\n"); fflush(0);
		return -1;
	}
	return 0;
}

void cbtimer_stop(cbtimer_t *tim)
{
	if (atomic_load(&tim->status) == CBTIMER_RUN) {
		atomic_store(&tim->status, CBTIMER_STOP);
		printf("Stopping timer %s "
//				"(0x%0x) "
				"%ld\n",
				tim->name,
//				tim,
				(long int)tim->timerId); fflush(0);
	}
}

int cbtimer_is_expired(cbtimer_t *tim)
{
	int ovr = timer_getoverrun(&tim->status);
	printf("timer_getoverrun = %d\n", ovr); fflush(0);
	return (ovr > 0);
}
