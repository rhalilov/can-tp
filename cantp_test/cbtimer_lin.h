/*
 * cbtimer_lin.h
 *
 *  Created on: May 20, 2022
 *      Author: refo
 */

#ifndef _CBTIMER_LIN_H_
#define _CBTIMER_LIN_H_

enum cbtimer_status {
	CBTIMER_RUN = 1,
	CBTIMER_STOP = 2
};

typedef struct cbtimer_s {
	atomic_int status;
	int data;
	timer_t timerId;
	char *name;
	void (*cb)(struct cbtimer_s *timer);
	void *cb_params;
} cbtimer_t;

int cbtimer_set_cb(cbtimer_t *tim, void (*cb)(struct cbtimer_s *timer),
														void *cb_params);
void cbtimer_set_name(cbtimer_t *tim, char *name);
int cbtimer_start(cbtimer_t *tim, long us);
void cbtimer_stop(cbtimer_t *tim);

#endif /* _CBTIMER_LIN_H_ */
