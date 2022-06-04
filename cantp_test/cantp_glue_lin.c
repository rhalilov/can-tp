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

static const char *cantp_fc_flow_status_enum_str[] = {
		FOREACH_CANTP_FC_FLOW_STATUS(GENERATE_STRING)
};

int cantp_rcvr_params_init(cantp_rxtx_status_t *ctx, uint8_t rx_bs, long st_min_us)
{
	ctx->bs_rcvr = rx_bs;
	if (st_min_us == 0) {
		ctx->st_rcvr = 0;
		return 0;
	}
	if (st_min_us < 100) {
		printf("\033[0;31mERROR:\033[0m "
				"STmin parameter should be more than 100μs\n"); fflush(0);
		return -1;
	}
	if (st_min_us > 9000000) {
		printf("\033[0;31mERROR:\033[0m "
				"STmin parameter should be less 900ms\n"); fflush(0);
		return -1;
	}
	if (st_min_us < 900) {
		ctx->st_rcvr = (uint8_t)(st_min_us / 100) + (uint8_t)0xf0;
		printf("STmin=%x\n", ctx->st_rcvr);
	}
}

void print_cantp_frame(cantp_frame_t cantp_frame)
{
	printf("\033[0;32m%s frame\033[0m ",
					cantp_frame_t_enum_str[cantp_frame.n_pci_t]);
	uint8_t datalen;
	uint8_t *dataptr;
	switch (cantp_frame.n_pci_t) {
	case CANTP_SINGLE_FRAME: {
			printf("len=%d ", cantp_frame.sf.len);
			datalen = CANTP_SF_NUM_DATA_BYTES;
			dataptr = cantp_frame.sf.d;
		} break;
	case CANTP_FIRST_FRAME: {
			printf("len=%d ", cantp_ff_len_get(&cantp_frame));
			datalen = CANTP_FF_NUM_DATA_BYTES;
			dataptr = cantp_frame.ff.d;
		} break;
	case CANTP_CONSEC_FRAME: {
			printf("SN=%x ", cantp_frame.cf.sn);
			datalen = CANTP_CF_NUM_DATA_BYTES;
			dataptr = cantp_frame.cf.d;
		} break;
	case CANTP_FLOW_CONTROLL: {
			printf("FlowStatus=%s BlockSize=%d STmin=%x ",
					cantp_fc_flow_status_enum_str[cantp_frame.fc.fs],
					cantp_frame.fc.bs,
					cantp_frame.fc.st);
			datalen = 0;
			dataptr = NULL;
		} break;
	}
	for (uint8_t i=0; i < datalen; i++) {
		printf("0x%02x ", dataptr[i]);
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

void cantp_usleep(long tout_us)
{
	usleep(tout_us);
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

int cantp_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data, long tout_us)
{
	return fake_can_tx(id, idt, dlc, data, tout_us);
}
