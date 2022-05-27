/*
 * cantp_glue_lin.c
 *
 *  Created on: May 21, 2022
 *      Author: refo
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

#include "cbtimer_lin.h"
#include "fake_can_linux.h"
#include "can-tp.h"

static const char *cantp_frame_t_enum_str[] = {
		FOREACH_CANTP_N_PCI_TYPE(GENERATE_STRING)
};

void print_cantp_frame(cantp_frame_t cantp_frame)
{
	printf("\033[0;32m%s frame\033[0m ",
					cantp_frame_t_enum_str[cantp_frame.n_pci_t]);
	for (uint8_t i=0; i < 8; i++) {
		printf("0x%02x ", cantp_frame.u8[i]);
	}
	printf("\n");fflush(0);
}

int cantp_timer_start(void *timer, char *name, long tout_us)
{
	cbtimer_t *t;
	t = (cbtimer_t *)timer;
	fflush(0);
	cbtimer_set_name(t, name);
	return cbtimer_start(t, tout_us);
}

int cantp_is_timer_expired(void *timer)
{
	cbtimer_t *t;
	return cbtimer_is_expired(t);
}

void cantp_tx_t_cb(cbtimer_t *tim)
{
	cantp_rxtx_status_t *ctx = (cantp_rxtx_status_t *)(tim->cb_params);
	cantp_tx_timer_cb(ctx);
}

void cantp_timer_stop(void *timer)
{
	cbtimer_t *t;
	t = (cbtimer_t *)timer;
	cbtimer_stop(t);
}

int cantp_can_tx_nb(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data)
{
	return fake_can_tx_nb(id, idt, dlc, data);
}

int cantp_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data)
{
	return fake_can_tx(id, idt, dlc, data);
}

int cantp_can_wait_txdone(long tout_us)
{
	return fake_can_wait_txdone(tout_us);
}
