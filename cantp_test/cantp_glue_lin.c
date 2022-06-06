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
#include <sys/mman.h>

#include "cbtimer_lin.h"
#include "fake_can_linux.h"
#include "can-tp.h"

static const char *cantp_frame_t_enum_str[] = {
		FOREACH_CANTP_N_PCI_TYPE(GENERATE_STRING)
};

static const char *cantp_fc_flow_status_enum_str[] = {
		FOREACH_CANTP_FC_FLOW_STATUS(GENERATE_STRING)
};

int cantp_rcvr_params_init(cantp_rxtx_status_t *ctx, cantp_params_t *par, char *name)
{
	msync(par, sizeof(cantp_params_t), MS_SYNC);
	cantp_logd("cantp_rcvr_params_init %s\n", name); fflush(0);
	ctx->params = par;

	if (ctx->params->st_min_us == 0) {
		ctx->params->st_min = 0;
	} else
	if (ctx->params->st_min_us < 100) {
		cantp_logd("\033[0;31mERROR:\033[0m "
				"STmin=%d parameter should be more than 100μs\n",
				ctx->params->st_min_us); fflush(0);
		return -1;
	} else
	if ( ctx->params->st_min_us > 127000 ) {
		//Should be handled by FC.WAIT frame transmissions

		//The BS and STmin parameters of FC frame will be ignored
		//But for the sender side it is important to send only one Consecutive
		//Frame and than wait for a reception of a Flow Control frame which
		//in this case should be FC.WAIT frame.
		ctx->params->block_size = 1;
		ctx->params->st_min = 0;

		if (ctx->params->wft_tim_us < 127000) {
			cantp_logd("\033[0;31mERROR:\033[0m "
				"'wft_tim_us' parameter is less than 127ms.\n"); fflush(0);
			return -1;
		}
		ctx->params->wft_num = ctx->params->st_min_us / ctx->params->wft_tim_us;
		if (ctx->params->wft_num > ctx->params->wft_max) {
			cantp_logd("\033[0;31mERROR:\033[0m "
				"The number FC.WAIT frames %d "
				"needed to achieve STmin=%d is greater than wft_max=%d \n",
				ctx->params->wft_num, ctx->params->st_min_us, ctx->params->wft_max); fflush(0);
			return -1;
		}
//		cantp_logd("wft_num=%d\n", ctx->params->wft_num);fflush(0);
	} else
//	if (ctx->params->st_min_us > 900000) {
//		cantp_logd("\033[0;31mERROR:\033[0m "
//				"STmin parameter should be less 900ms\n"); fflush(0);
//		return -1;
//	}
	if ( (ctx->params->st_min_us > 900) && (ctx->params->st_min_us < 1000) ) {
		if (ctx->params->st_min_us >= 950) {
			ctx->params->st_min_us = 1000;
		} else {
			ctx->params->st_min_us = 900;
		}
	} else
	if ( (ctx->params->st_min_us >= 1000) && (ctx->params->st_min_us <= 127000) ) {
		ctx->params->st_min = ctx->params->st_min_us / 1000;
	} else
	if (ctx->params->st_min_us <= 900) {
//		ctx->st_rcvr = (uint8_t)(st_min_us / 100) + (uint8_t)0xf0;
		ctx->params->st_min = (uint8_t)(ctx->params->st_min_us / 100) + (uint8_t)0xf0;
		cantp_logd("STmin=%x\n", ctx->params->st_min);
	}
	cantp_logd(	"block_size = %d\n"
			"st_min_us = %d\n"
			"st_min = %x\n"
			"wft_num = %d\n"
			"wft_max = %d\n"
			"wft_tim_us = %d\n",
			ctx->params->block_size,
			ctx->params->st_min_us,
			ctx->params->st_min,
			ctx->params->wft_num,
			ctx->params->wft_max,
			ctx->params->wft_tim_us);fflush(0);
	return 0;
}

void print_cantp_frame(cantp_frame_t cantp_frame)
{
	cantp_logi("\033[0;32m%s frame\033[0m ",
					cantp_frame_t_enum_str[cantp_frame.n_pci_t]);
	uint8_t datalen;
	uint8_t *dataptr;
	switch (cantp_frame.n_pci_t) {
	case CANTP_SINGLE_FRAME: {
			cantp_logi("len=%d ", cantp_frame.sf.len);
			datalen = CANTP_SF_NUM_DATA_BYTES;
			dataptr = cantp_frame.sf.d;
		} break;
	case CANTP_FIRST_FRAME: {
			cantp_logi("len=%d ", cantp_ff_len_get(&cantp_frame));
			datalen = CANTP_FF_NUM_DATA_BYTES;
			dataptr = cantp_frame.ff.d;
		} break;
	case CANTP_CONSEC_FRAME: {
			cantp_logi("SN=%x ", cantp_frame.cf.sn);
			datalen = CANTP_CF_NUM_DATA_BYTES;
			dataptr = cantp_frame.cf.d;
		} break;
	case CANTP_FLOW_CONTROLL: {
			cantp_logi("FlowStatus=\033[0;33m%s\033[0m BlockSize=%d STmin=%x ",
					cantp_fc_flow_status_enum_str[cantp_frame.fc.fs],
					cantp_frame.fc.bs,
					cantp_frame.fc.st);
			datalen = 0;
			dataptr = NULL;
		} break;
	}
	for (uint8_t i=0; i < datalen; i++) {
		cantp_logi("0x%02x ", dataptr[i]);
	}
	cantp_logi("\n");fflush(0);
}

int cantp_timer_start(void *timer, char *name, long tout_us)
{
	cbtimer_t *t;
	t = (cbtimer_t *)timer;
	fflush(0);
	cantp_logd("Starting timer %s %ldμs\n", t->name, tout_us);
	cbtimer_set_name(t, name);
	return cbtimer_start(t, tout_us);
}

void cantp_timer_stop(void *timer)
{
	cbtimer_t *t;
	t = (cbtimer_t *)timer;
	cbtimer_stop(t);
	cantp_logd("Stopped timer %s\n", t->name);
}

int cantp_is_timer_expired(void *timer)
{
	cbtimer_t *t;
	if (cbtimer_is_expired(t) > 0) {
		cantp_logd("timer %s expired\n", t->name);
		return 1;
	}
	return 0;
}

void cantp_usleep(long tout_us)
{
	usleep(tout_us);
}

void cantp_sndr_t_cb(cbtimer_t *tim)
{
	cantp_rxtx_status_t *ctx = (cantp_rxtx_status_t *)(tim->cb_params);
	cantp_sndr_timer_cb(ctx);
}

void cantp_rcvr_t_cb(cbtimer_t *tim)
{
	cantp_rxtx_status_t *ctx = (cantp_rxtx_status_t *)(tim->cb_params);
	cantp_rcvr_timer_cb(ctx);
}

int cantp_can_tx_nb(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data)
{
	return fake_can_tx_nb(id, idt, dlc, data);
}

int cantp_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data, long tout_us)
{
	return fake_can_tx(id, idt, dlc, data, tout_us);
}
