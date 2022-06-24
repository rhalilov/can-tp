/*
 * can-tp.c
 *
 *  Created on: May 15, 2022
 *      Author: refo
 *
 *      for timer see:
 *      https://man7.org/linux/man-pages/man2/timer_create.2.html
 *      https://stackoverflow.com/questions/64429205/how-to-use-sigev-thread-sigevent-for-linux-timers-expiration-handling-in-c
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

//#include "cantp_config.h"
#include "can-tp.h"

static const char *cantp_state_enum_str[] = {
		FOREACH_CANTP_STATE(GENERATE_STRING)
};

static inline void cantp_sndr_set_state(cantp_rxtx_status_t *ctx, uint8_t state)
{
	cantp_logd("\t\033[0;36mCAN-TP Sender: \033[1;36mSet State to: "
			"\033[1;33m%s\033[0m\n", cantp_state_enum_str[state]); fflush(0);
	ctx->sndr.state = state;
	cantp_sndr_state_sem_give(ctx);
}

static inline void cantp_rcvr_set_state(cantp_rxtx_status_t *ctx, uint8_t state)
{
	cantp_logd("\t\033[0;33mCAN-TP Receiver: \033[1;36mSet State to: "
				"\033[1;33m%s\033[0m\n", cantp_state_enum_str[state]); fflush(0);
	ctx->rcvr.state = state;
}

static inline uint8_t cantp_sndr_get_state(cantp_rxtx_status_t *ctx)
{
	return ctx->sndr.state;
}

static inline uint8_t cantp_rcvr_get_state(cantp_rxtx_status_t *ctx)
{
	return ctx->rcvr.state;
}

static inline int cantp_sndr_wait_state_change(cantp_rxtx_status_t *ctx, uint32_t tout_us)
{
	return cantp_sndr_state_sem_take(ctx, tout_us);
}

void cantp_sndr_timer_cb(cantp_rxtx_status_t *ctx)
{
	cantp_logd("\t\033[0;36mCAN-TP Sender Timer Callback State:\033[0m \033[1;33m%s\033[0m\n",
					cantp_state_enum_str[cantp_sndr_get_state(ctx)]);

	switch (cantp_sndr_get_state(ctx)) {
	case CANTP_STATE_FF_SENDING:
	case CANTP_STATE_CF_SENDING:
	case CANTP_STATE_SF_SENDING: {
			cantp_sndr_set_state(ctx, CANTP_STATE_IDLE);
			cantp_sndr_result_cb(CANTP_RESULT_N_TIMEOUT_As);
		} break;
//	case CANTP_STATE_FF_SENT: {
//			cantp_set_sndr_state(ctx, CANTP_STATE_IDLE);
//			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_Bs);
//		}
//		break;
	case CANTP_STATE_FF_FC_WAIT: {
			cantp_sndr_set_state(ctx, CANTP_STATE_IDLE);
			cantp_sndr_result_cb(CANTP_RESULT_N_TIMEOUT_Bs);
		} break;
	} //switch (ctx->tx_state.state)

}

void cantp_rcvr_timer_cb(cantp_rxtx_status_t *ctx)
{
	cantp_logi("CAN-TP Receiver: State: \033[1;33m%s\033[0m ",
			cantp_state_enum_str[cantp_rcvr_get_state(ctx)]);
	//TODO: Make tests and implement !!!!!!


	//stopping N_As timer
//	cantp_timer_stop(ctx->rcvr.timer);
//	switch (ctx->sndr.state) {
//	case CANTP_STATE_SF_SENDING: {
//			ctx->sndr.state = CANTP_STATE_IDLE;
//			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_A);
//		}
//		break;
//	case CANTP_STATE_FF_SENT: {
//			ctx->sndr.state = CANTP_STATE_IDLE;
//			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_A);
//		}
//		break;
//	case CANTP_STATE_FF_FC_WAIT: {
//			ctx->sndr.state = CANTP_STATE_IDLE;
//			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_Bs);
//		}
//		break;
//	} //switch (ctx->tx_state.state)

}

static inline void sndr_send_cf_afrer_cf(cantp_rxtx_status_t *ctx)
{
//	cantp_logi("cantp_send_cf_afrer_cf \n"); fflush(0);
	int remaining = ctx->sndr.len - ctx->sndr.index;
	uint8_t n_to_send; //Number of bytes to send with this CAN frame

	if (remaining > CANTP_CF_NUM_DATA_BYTES) {
		n_to_send = CANTP_CF_NUM_DATA_BYTES;
	} else {
		n_to_send = remaining;
	}

	//Now should prepare to send the next Consecutive Frame
	cantp_frame_t tx_frame = { 0 };
	tx_frame.n_pci_t = CANTP_CONSEC_FRAME;
	tx_frame.cf.sn = ctx->sndr.sn;

	//copy the data to CAN tx_frame
	for (uint8_t i=0; i < n_to_send; i++) {
		tx_frame.cf.d[i] = ctx->sndr.data[ctx->sndr.index++];
	}

	//Stopping timer N_Cs
	cantp_logd("\t\033[0;36mCAN-TP Sender:\033[0m (S7.1)");
	cantp_timer_stop(ctx->sndr.timer); fflush(0);

	//Starting timer N_As
	cantp_logd("\t\033[0;36mCAN-TP Sender:\033[0m (S7.2)");
	cantp_timer_start(ctx->sndr.timer, "N_As", 1000 * CANTP_N_AS_TIMER_MS);
	fflush(0);

	cantp_sndr_set_state(ctx, CANTP_STATE_CF_SENDING);
	cantp_logi("\033[0;36mCAN-TP Sender: "
			"\033[0;32m(S7.3)Sending from\033[0m ID=0x%06x IDt=%d ",
			ctx->params->id, ctx->params->idt);
	print_cantp_frame(tx_frame); fflush(0);

	cantp_can_tx_nb(ctx->params->id, ctx->params->idt, 8, tx_frame.u8);
	//
}

void cantp_cantx_confirm_cb(cantp_rxtx_status_t *ctx)
{
	//The Sender transmits only Single Frame, First Frame and Consecutive Frames
	//so only this three types of frames can receive a transmit confirmation
//	cantp_logd("Transmission Confirmed (PID=%d)State: \033[1;33m%s\033[0m\n",
//			getpid(), cantp_state_enum_str[ctx->sndr.state]); fflush(0);

	switch(cantp_sndr_get_state(ctx)) {
	case CANTP_STATE_SF_SENDING: {
			cantp_sndr_set_state(ctx, CANTP_STATE_IDLE);
			cantp_timer_stop(ctx->sndr.timer);
			cantp_sndr_result_cb(CANTP_RESULT_N_OK);
		} break;
	case CANTP_STATE_FF_SENDING: {
			//First Frame is sent only from the Sender peer
			//We received a confirmation from data link layer
			//that the FF has being acknowledged (sent)
			//ISO 15765-2:2016 Figure 11 step(Key) 2
			cantp_logd("\t\033[0;36mCAN-TP Sender:\033[0m "
				"(S2.1)Received TX confirmation of First Frame\n"); fflush(0);

			//so we are stopping N_As timer
			cantp_logd("\t\033[0;36mCAN-TP Sender:\033[0m (S2.2)");
			cantp_timer_stop(ctx->sndr.timer); fflush(0);

			//starting new timer (N_Bs) for receiving Flow Control frame
			cantp_logd("\t\033[0;36mCAN-TP Sender:\033[0m (S2.3)");
			cantp_timer_start(ctx->sndr.timer, "N_Bs",
										1000 * CANTP_N_BS_TIMER_MS); fflush(0);

			//Setting the sequence number so that the next transmission
			//of the Consecutive Frame start with 1
			ctx->sndr.sn = 1;
			ctx->sndr.index = CANTP_FF_NUM_DATA_BYTES;

			cantp_sndr_set_state(ctx, CANTP_STATE_FF_FC_WAIT);
			//Now Sender should wait for Flow Control frame
		} break;
//	case CANTP_STATE_FC_SENDING: {
//			//Flow Control frame is sent only from the Receiver
//			//Since sending the FC frame from the Receiver is using
//			//blocking function cantp_can_tx() it is not expected to
//			//come here.
//			cantp_timer_log("\033[0;33m-------CAN-TP Receiver:\033[0m "
//					"(R4.1)Received TX confirmation of Flow Control frame\n");fflush(0);
//		} break;
	case CANTP_STATE_CF_SENT:
	case CANTP_STATE_CF_SENDING: {
			//Sender has received a confirmation for transmission of a CF
			cantp_logd("\t\033[0;36mCAN-TP Sender:\033[0m "
							"(S6.1)Received TX confirmation of Consecutive Frame\n");

			//stopping N_As timer
			cantp_logd("\t\033[0;36mCAN-TP Sender:\033[0m (S6.2)");
			cantp_timer_stop(ctx->sndr.timer); fflush(0);

			//Checking if it is Last Consecutive Frame
			int remaining = ctx->sndr.len - ctx->sndr.index;
			if (remaining < 0) {
				cantp_logd("\033[0;36mCAN-TP Sender: \033[0m "
						"Error: Wrong number of remaining bytes %d", remaining);
				//TODO: Error handling
				return;
			} else if (remaining == 0) {
				//the last Consecutive frame (for which we just received the
				//transmission confirmation) were the Last Consecutive Frame
				cantp_logd("\033[0;36mCAN-TP Sender:\033[0m (16.1)\n"); fflush(0);
				cantp_sndr_set_state(ctx, CANTP_STATE_IDLE);
				cantp_sndr_tx_done_cb();
				break;
			}

			ctx->sndr.sn ++;
			if (ctx->sndr.sn > 0x0f) {
				ctx->sndr.sn = 0;
			}

			//check if Block Size (BS) parameter form the Receiver is > 0
			//that could mean the Receiver will expect BS counts of
			//Consecutive Frames and then (the Receiver) will have to send
			//a Flow Control frame
			if (ctx->sndr.bs_pair > 0) {
				ctx->sndr.bl_index ++;
				cantp_logd("\033[0;36mCAN-TP Sender:\033[0m "
							"BlockSize=%d block sent so far %d\n",
								ctx->sndr.bs_pair, ctx->sndr.bl_index); fflush(0);

				if (ctx->sndr.bl_index >= ctx->sndr.bs_pair) {
					ctx->sndr.bl_index = 0;
					//We should wait for Flow Control Frame from the Receiver
					cantp_logd("\033[0;36mCAN-TP Sender:\033[0m "
							"(8.1)Waiting for Flow Control Frame from Receiver\n");

					cantp_logd("\033[0;36mCAN-TP Sender:\033[0m (8.2)");
					cantp_timer_start(ctx->sndr.timer, "N_Bs",
										1000 * CANTP_N_BS_TIMER_MS); fflush(0);
					cantp_sndr_set_state(ctx, CANTP_STATE_CF_FC_WAIT);
					break;
				}
			}
			cantp_sndr_set_state(ctx, CANTP_STATE_CF_SENT);

//			cantp_logi("\033[0;36mCAN-TP Sender: "
//									"Sending next Consecutive Frame\033[0m\n");

			if (ctx->sndr.st_tim_us > 0) {
				cantp_logd("\033[0;36mCAN-TP Sender:\033[0m "
						"(S6.3)STmin timer Start\n"); fflush(0);
				cantp_usleep(ctx->sndr.st_tim_us);
				cantp_logd("\033[0;36mCAN-TP Sender:\033[0m "
						"(S6.4)STmin timer ended\n"); fflush(0);

			}

			//starting timer N_Cs
			cantp_logd("\t\033[0;36mCAN-TP Sender:\033[0m (S6.5)");
			cantp_timer_start(ctx->sndr.timer, "N_Cs",
									1000 * CANTP_N_CS_TIMER_MS); fflush(0);


			sndr_send_cf_afrer_cf(ctx);
		} break;
	}
}

static inline void sndr_send_cf_afrer_fc(cantp_frame_t *rx_frame,
													cantp_rxtx_status_t *ctx)
{
	//The Consecutive frame is send only from the Sender side and
	//only after a Flow Control frame or other Consecutive Frame

	//Stopping N_Cs timer
	cantp_logd("\t\033[0;36mCAN-TP Sender:\033[0m (S5.1)");
	cantp_timer_stop(ctx->sndr.timer); fflush(0);

	//starting timer N_As
	cantp_logd("\t\033[0;36mCAN-TP Sender:\033[0m (S5.2)");
	cantp_timer_start(ctx->sndr.timer, "N_As", 1000 * CANTP_N_CS_TIMER_MS); fflush(0);

	//Checking if it will be the Last Consecutive Frame
	int remaining = ctx->sndr.len - ctx->sndr.index;
	if (remaining <= 0) {
		cantp_logd("\033[0;36mCAN-TP Sender: \033[0m "
				"Error: Wrong number of remaining bytes %d", remaining);
		//TODO: Error handling
		return;
	}
	cantp_logv("\t\t\033[0;36mCAN-TP Sender:\033[0m Len=%d Index=%d Remaining=%d\n",
			ctx->sndr.len, ctx->sndr.index, remaining); fflush(0);

	uint8_t n_to_send; //Number of bytes to send with this CAN frame
	if (remaining > CANTP_CF_NUM_DATA_BYTES) {
		n_to_send = CANTP_CF_NUM_DATA_BYTES;
	} else {
		n_to_send = remaining;
	}

	//Now should prepare to send the next Consecutive Frame
	cantp_frame_t tx_frame = { 0 };
	tx_frame.n_pci_t = CANTP_CONSEC_FRAME;
	tx_frame.cf.sn = ctx->sndr.sn;

	//copy the data to CAN tx_frame
	cantp_logv("\t\t\033[0;36mCAN-TP Sender:\033[0m Copy Data: ");
	for (uint8_t i=0; i < n_to_send; i++) {
		tx_frame.cf.d[i] = ctx->sndr.data[ctx->sndr.index++];
		cantp_logv("0x%02x ", tx_frame.cf.d[i]);
	}
	cantp_logv("\n"); fflush(0);

	cantp_sndr_set_state(ctx, CANTP_STATE_CF_SENDING);

	cantp_logi("\033[0;36mCAN-TP Sender: \033[0;32m(S5.3)Sending from\033[0m "
			"ID=0x%06x IDt=%d DLC=%d",
			ctx->params->id, ctx->params->idt, 8);
	print_cantp_frame(tx_frame); fflush(0);

	cantp_can_tx_nb(ctx->params->id, ctx->params->idt, 8, tx_frame.u8);
	//wait for transmission to be confirmed cantp_cantx_confirm_cb()
}

static inline void rcvr_rx_first_frame(uint32_t id,
					uint8_t idt,
					uint8_t dlc,
					cantp_frame_t *cantp_rx_frame,
					cantp_rxtx_status_t *ctx)
{

	cantp_rcvr_set_state(ctx, CANTP_STATE_FF_RCVD);

	//Starting N_Br timer
	cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m (R2.2)");
	cantp_timer_start(ctx->rcvr.timer, "N_Br", 1000 * CANTP_N_BR_TIMER_MS); fflush(0);

	//update some connection parameters into Receiver state
	//Actually the FF carries only CAN-TP message length information
	ctx->rcvr.id_pair = id;//TODO: I still don't know what to do with this
				//so for now I am just passing it to the session layer
	ctx->rcvr.len = cantp_ff_len_get(cantp_rx_frame);
	ctx->rcvr.index = 0;
	ctx->rcvr.bl_index = 0;

	//Calling Session Layer to inform it for a coming CAN-TP segmented message
	if (cantp_rcvr_rx_ff_cb(id, idt, &ctx->rcvr.data, ctx->rcvr.len) < 0) {
		//Stopping timer N_Br
		cantp_timer_stop(ctx->rcvr.timer);
		cantp_rcvr_set_state(ctx, CANTP_STATE_IDLE);
		//TODO: Error checks of both Sender and Receiver sides
		//Maybe the receiver should just ignore that CAN Frame in
		//which is not interested
		return;
	}

	if (ctx->rcvr.data == NULL) {
		cantp_logd("\033[0;33mCAN-TP Receiver: "
				"Error: No buffer allocated from Session Layer\033[0m");
		//TODO: Send Abort to both Sender and Receivers Session Layer
		return;
	}

	cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m "
			"Session layer accepted the request. Copying data to context buffer:");
	for (uint8_t i=0; i < CANTP_FF_NUM_DATA_BYTES; i++) {
		ctx->rcvr.data[ctx->rcvr.index++] = cantp_rx_frame->ff.d[i];
		cantp_logi(" 0x%02x", cantp_rx_frame->ff.d[i]);
	}
	cantp_logi("\n"); fflush(0);

	//The Session Layer Receiver accepts this message

	ctx->rcvr.index = CANTP_FF_NUM_DATA_BYTES;

	//Stopping timer N_Br. This is the time of processing cantp_rcvd_ff_cb()
	cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m (R3.1)");
	cantp_timer_stop(ctx->rcvr.timer); fflush(0);

	//Starting timer N_Ar
	cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m (R3.2)");
	cantp_timer_start(ctx->rcvr.timer, "N_Ar", 1000 * CANTP_N_AR_TIMER_MS); fflush(0);

	//Preparing Flow Control frame
	cantp_frame_t rcvr_tx_fc_frame = { 0 };

	rcvr_tx_fc_frame.n_pci_t = CANTP_FLOW_CONTROLL;
	rcvr_tx_fc_frame.fc.bs = ctx->params->block_size;
	rcvr_tx_fc_frame.fc.st = ctx->params->st_min;
	rcvr_tx_fc_frame.fc.fs = CANTP_FC_FLOW_STATUS_CTS;
	cantp_rcvr_set_state(ctx, CANTP_STATE_FC_SENDING);

	cantp_logi("\033[0;33mCAN-TP Receiver: \033[0;32m(R3.3)Sending \033[0m");
	print_cantp_frame(rcvr_tx_fc_frame);

	//Sending FC frame
	if (cantp_can_tx(ctx->params->id, ctx->params->idt, 8, rcvr_tx_fc_frame.u8,
									1000 * CANTP_N_AR_TIMER_MS) < 0) {
		//Abort message reception and issue N_USData.indication
		//with <N_Result> = N_TIMEOUT_A
		cantp_sndr_result_cb(CANTP_RESULT_N_TIMEOUT_As);
	}
	cantp_rcvr_set_state(ctx, CANTP_STATE_FC_SENT);
	cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m "
			"(R4.1)TX Done \n"); fflush(0);

	//Stopping timer N_Ar
	cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m (R4.2)");
	cantp_timer_stop(ctx->rcvr.timer); fflush(0);
	//Starting N_Cr
	cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m (R4.3)");
	cantp_timer_start(ctx->rcvr.timer, "N_Cr", 1000 * CANTP_N_CR_TIMER_MS);
	//Now the Receiver should wait for first Consecutive Frame

}

static inline void sndr_rx_flow_control_frame(uint32_t id,
					uint8_t idt,
					uint8_t dlc,
					cantp_frame_t *cantp_rx_frame,
					cantp_rxtx_status_t *ctx)
{
	//Stopping N_Bs timer
	cantp_logd("\t\033[0;36mCAN-TP Sender:\033[0m (S4.2)");
	cantp_timer_stop(ctx->sndr.timer); fflush(0);

	//starting timer N_Cs
	cantp_logd("\t\033[0;36mCAN-TP Sender:\033[0m (S4.3)");
	cantp_timer_start(ctx->sndr.timer, "N_Cs", 1000 * CANTP_N_CS_TIMER_MS);

	//Saving connection parameters sent from the Receiver
	ctx->sndr.id_pair = id;	//Saving the pair (Receiver) ID. The IDT should
						//be same type as ours (Sender)
	ctx->sndr.bs_pair = cantp_rx_frame->fc.bs;
	ctx->sndr.st_pair = cantp_rx_frame->fc.st;
	if (ctx->sndr.st_pair < 0x80) {
		ctx->sndr.st_tim_us = 1000 * ctx->sndr.st_pair;
	} else if ((ctx->sndr.st_pair >= 0xf1) && (ctx->sndr.st_pair <= 0xf9)) {
		ctx->sndr.st_tim_us = 100 * (ctx->sndr.st_pair - 0xf0);
	} else {
		cantp_logi("\033[0;31mERROR:\033[0m: incompatible value of STmin\n");fflush(0);
		//TODO: Error check !!!
		return;
	}
	switch (cantp_rx_frame->fc.fs) {
	case CANTP_FC_FLOW_STATUS_CTS: {
			//Continue to send
			sndr_send_cf_afrer_fc(cantp_rx_frame, ctx);
		} break;
	case CANTP_FC_FLOW_STATUS_WAIT: {
			//Just restarting N_Bs timer
			cantp_logd("\033[0;36mCAN-TP Sender:\033[0m (S10.3)");
			cantp_timer_start(ctx->sndr.timer, "N_Bs", 1000 * CANTP_N_BS_TIMER_MS);

		} break;
	case CANTP_FC_FLOW_STATUS_OVF: {
			//TODO: Implementation of CANTP_FC_FLOW_STATUS_OVF
		} break;
	}

}

static inline void rcvr_rx_consecutive_frame(uint32_t id,
					uint8_t idt,
					uint8_t dlc,
					cantp_frame_t *cantp_rx_frame,
					cantp_rxtx_status_t *ctx)
{
	cantp_frame_t rcvr_tx_frame = { 0 };

	//Checking if Consecutive frame is addressed to us
	if (ctx->rcvr.id_pair != id) {
		cantp_logi("\033[0;33mCAN-TP Receiver:\033[0m "
				"Ignoring frame from ID=0x%06x expected was ID=0x%06x",
				id, ctx->rcvr.id_pair);
		return;
	}

	//Stopping N_Cr timer
	cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m (R6.2)");
	cantp_timer_stop(ctx->rcvr.timer); fflush(0);

	//We are not starting the next timer here because first we
	//need to decide is: are we waiting for next Consecutive Frame
	//or need to send Flow Control frame

	//Processing the received Consecutive Frame
	ctx->rcvr.sn ++;
	if (ctx->rcvr.sn > 0x0f) {
		ctx->rcvr.sn = 0;
	}
	if (ctx->rcvr.sn != cantp_rx_frame->cf.sn) {
		cantp_logd("\033[0;33mCAN-TP Receiver: "
				"\033[0;31mError: The received SN=%d doesn't match local %d\033[0m\n",
				ctx->rcvr.sn, cantp_rx_frame->cf.sn);
		//TODO: Error handling
		return;
	}
	cantp_logd("\033[0;33mCAN-TP Receiver: Sequence number %x is correct.\033[0m\n",
			cantp_rx_frame->cf.sn);

	//Checking if it is Last Consecutive Frame
	int remaining = ctx->rcvr.len - ctx->rcvr.index;
	if (remaining <= 0) {
		cantp_logd("\033[0;33mCAN-TP Receiver: \033[0m "
				"Error: Wrong number of remaining bytes %d", remaining);
		//TODO: Error handling
		return;
	}
	uint8_t bytes_rcvd = (remaining < CANTP_CF_NUM_DATA_BYTES)?
			remaining:CANTP_CF_NUM_DATA_BYTES;

	cantp_logd("\033[0;33mCAN-TP Receiver: "
			"Copying %d bytes to context buffer: \033[0m", bytes_rcvd);

	for (uint8_t i=0; i < bytes_rcvd; i++) {
		ctx->rcvr.data[ctx->rcvr.index++] = cantp_rx_frame->cf.d[i];
		cantp_logi(" 0x%02x", cantp_rx_frame->cf.d[i]);
	}
	cantp_logi("\n"); fflush(0);

	if (remaining <= CANTP_CF_NUM_DATA_BYTES) {
		//It is the last Consecutive frame
		cantp_rcvr_set_state(ctx, CANTP_STATE_TX_DONE);
		cantp_received_cb(ctx, ctx->params->id, ctx->params->idt,
												ctx->rcvr.data, ctx->rcvr.len);
		return;
	}

	//TODO: Maybe need to check (ask Session Layer) for sending
	//a Flow Control Wait frame here. There is no such call in
	//ISO 15765-2:2016 : Figure 11
	//Maybe this "Waits" should be implemented based on global timing
	//parameters of the Receiver which defines times higher than STmin
	//and N_Cr
	//We will do that only for testing purposes !!!

	//Checking if Wait Frame is enabled in the Receiver side
	cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m wft_num=%d\n", ctx->params->wft_num);
	if (ctx->params->wft_num > 0) {
		ctx->rcvr.wft_cntr = 0;
		do {
			cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m wft_cntr=%d\n",
														ctx->rcvr.wft_cntr);

			//The Flow Control Wait frame should be sent for not more than N_Br
			//For a timer we will use the value of the STmin which will define
			//timeout for not more than 900 milliseconds (to not exceed N_Br)

			cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m "
					"wft_cntr=%d Sleeping for %dμs \n",
					ctx->rcvr.wft_cntr, ctx->params->wft_tim_us); fflush(0);
			cantp_usleep(ctx->params->wft_tim_us);
			cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m Sleeping ended \n"); fflush(0);

			//Now send FC.WAIT frame
			rcvr_tx_frame.n_pci_t = CANTP_FLOW_CONTROLL;
			rcvr_tx_frame.fc.fs = CANTP_FC_FLOW_STATUS_WAIT;
			rcvr_tx_frame.fc.bs = 0;
			rcvr_tx_frame.fc.st = 0;

			cantp_logi("\033[0;33mCAN-TP Receiver: (R9.3)Sending \033[0m");
			print_cantp_frame(rcvr_tx_frame);

			//(R9.2) N_Ar timer is embedded in blocking sending
			//Sending FC frame
			if (cantp_can_tx(ctx->params->id, ctx->params->idt, 8, rcvr_tx_frame.u8,
											1000 * CANTP_N_AR_TIMER_MS) < 0) {
				//Abort message reception and issue N_USData.indication
				//with <N_Result> = N_TIMEOUT_A
				cantp_sndr_result_cb(CANTP_RESULT_N_TIMEOUT_As);
			}

			cantp_rcvr_set_state(ctx, CANTP_STATE_FC_SENT);

			cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m "
					"(R10.1)Received TX confirmationn of Flow Controll frame\n");

			ctx->rcvr.wft_cntr ++;

			//Check if we exceeded the maximum number of Flow Control Wait frames
			if (ctx->rcvr.wft_cntr > ctx->params->wft_num) {
//				ctx->rcvr.wft_cntr = 0;
				//Next is wait for a Consecutive Frame
				//Starting N_Cr
				cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m (R10.3)");
				cantp_timer_start(ctx->rcvr.timer, "N_Cr", 1000 * CANTP_N_CR_TIMER_MS);
				break;
			} else {
				//Next is an another Flow Control Wait frame
				//Starting N_Br
				cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m (R10.3)");
				cantp_timer_start(ctx->rcvr.timer, "N_Br", 1000 * CANTP_N_BR_TIMER_MS);

			}

			//Check if following is an another Flow Control Wait frame
			//or Consecutive Frame

		} while (ctx->rcvr.wft_cntr < ctx->params->wft_num);

	}

	//Checking if we (Receiver) need to send Flow Control frame or
	//wait for next Consecutive Frame from the Sender
	//First Checking if we need to count blocks at all
	if (ctx->params->block_size > 0) {
		ctx->sndr.bl_index ++;
		cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m "
					"BlockSize=%d block received till now %d\n",
					ctx->params->block_size, ctx->sndr.bl_index); fflush(0);

		if (ctx->sndr.bl_index >= ctx->params->block_size) {

			//(R8.3)Do not need to start N_Br timer here because it is just few
			//lines of code that are following

			ctx->sndr.bl_index = 0;

			rcvr_tx_frame.n_pci_t = CANTP_FLOW_CONTROLL;
			rcvr_tx_frame.fc.fs = CANTP_FC_FLOW_STATUS_CTS;

			rcvr_tx_frame.fc.bs = ctx->params->block_size;
			rcvr_tx_frame.fc.st = ctx->params->st_min;

			cantp_logi("\033[0;33mCAN-TP Receiver: (R11.3)Sending \033[0m");
			print_cantp_frame(rcvr_tx_frame);

			//(R11.2) N_Ar timer is embedded in blocking sending
			//Sending FC frame
			if (cantp_can_tx(ctx->params->id, ctx->params->idt, 8, rcvr_tx_frame.u8,
											1000 * CANTP_N_AR_TIMER_MS) < 0) {
				//Abort message reception and issue N_USData.indication
				//with <N_Result> = N_TIMEOUT_A
				cantp_sndr_result_cb(CANTP_RESULT_N_TIMEOUT_As);
			}

			cantp_rcvr_set_state(ctx, CANTP_STATE_FC_SENT);
			cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m "
					"(R12.1)Received TX confirmationn of Flow Controll frame\n");

			//Starting N_Cr
			cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m (R12.3)");
			cantp_timer_start(ctx->rcvr.timer, "N_Cr", 1000 * CANTP_N_CR_TIMER_MS);

			//Now the Receiver should wait for a Consecutive Frame
			//or other Flow Control frame
			return;
		}
	} //if (ctx->params->block_size > 0)




	//We are waiting for next Consecutive Frame

	//Starting N_Cr timer again
	cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m (R6.3)");
	cantp_timer_start(ctx->rcvr.timer, "N_Cr",
									1000 * CANTP_N_CR_TIMER_MS); fflush(0);

}

void cantp_canrx_cb(uint32_t id,
					uint8_t idt,
					uint8_t dlc,
					uint8_t *data,
					cantp_rxtx_status_t *ctx)
{
	//TODO: We need to make some ID checks here

	//TODO: check validity of dlc
	cantp_frame_t rx_frame = { 0 };//, rcvr_tx_frame = { 0 };
	for (uint8_t i=0; i < dlc; i++) {
		rx_frame.u8[i] = data[i];
	}

	switch (rx_frame.n_pci_t) {
	case CANTP_SINGLE_FRAME: {
			//Single Frame can be received only from the Receiver
			cantp_logi("\033[0;33mCAN-TP Receiver:\033[1;33m "
					"(R2.1)Received from\033[0m ID=0x%06x IDt=%d DLC=%d ",
															id, idt, dlc);
			print_cantp_frame(rx_frame); fflush(0);
			//TODO: Check the status of the Receiver and ignore the frame if it is
			//still in reception of another segmented message

			//present the received data to the Session Layer
			//(Receiver N_USData.ind)
			cantp_rcvr_set_state(ctx, CANTP_STATE_RX_DONE);
			cantp_received_cb(ctx, id, idt, rx_frame.sf.d, rx_frame.sf.len);

		} break;
	case CANTP_FIRST_FRAME: {
			//First Frame can be received only from the Receiver side
			cantp_logi("\033[0;33mCAN-TP Receiver:\033[1;33m (R2.1)Received from\033[0m "
					"ID=0x%06x IDt=%d DLC=%d ", id, idt, dlc);
			print_cantp_frame(rx_frame); fflush(0);
			cantp_logd("\033[0;33mCAN-TP Receiver: State: \033[1;33m%s\033[0m\n",
							cantp_state_enum_str[cantp_rcvr_get_state(ctx)]);

			//The receiver can accept a FF only if it is in IDLE state
			//i.e. not receiving another CAN-TP message
			if (cantp_rcvr_get_state(ctx) == CANTP_STATE_IDLE) {
				rcvr_rx_first_frame(id, idt, dlc, &rx_frame, ctx);
			} else {
				cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m "
						"ERROR: Not a correct Sate\n");
			}
		} break;
	case CANTP_FLOW_CONTROLL: {
			//Flow Control frame can be received only from the Sender side

			uint8_t state = cantp_sndr_get_state(ctx);
			cantp_logv("\t\033[0;36mCAN-TP Sender: State: \033[1;33m%s\033[0m\n",
						cantp_state_enum_str[state]);
			if (state != CANTP_STATE_FF_FC_WAIT) {
				if (cantp_sndr_wait_state_change(ctx,
											1000 * CANTP_N_BS_TIMER_MS) < 0 ) {
					break;
				}
				state = cantp_sndr_get_state(ctx);
				cantp_logv("\t\033[0;36mCAN-TP Sender: State: \033[1;33m%s\033[0m\n",
										cantp_state_enum_str[state]);
				if (state != CANTP_STATE_FF_FC_WAIT) {
					break;
				}
			}
			cantp_logi("\033[0;36mCAN-TP Sender: \033[1;33m(S4.1)Received from\033[0m "
					"ID=0x%06x IDt=%d DLC=%d ", id, idt, dlc);
			print_cantp_frame(rx_frame); fflush(0);

			sndr_rx_flow_control_frame(id, idt, dlc, &rx_frame, ctx);
		} break;
	case CANTP_CONSEC_FRAME: {
			//Consecutive Frame can be received only from the Receiver
			cantp_logi("\033[0;33mCAN-TP Receiver: \033[1;33m(R6.1)Received from\033[0m "
					"ID=0x%06x IDt=%d DLC=%d ", id, idt, dlc);
			print_cantp_frame(rx_frame); fflush(0);

			cantp_logd("\033[0;33mCAN-TP Receiver: State: \033[1;33m%s\033[0m\n",
						cantp_state_enum_str[cantp_rcvr_get_state(ctx)]);

			if (cantp_rcvr_get_state(ctx) == CANTP_STATE_FC_SENT) {
				rcvr_rx_consecutive_frame(id, idt, dlc, &rx_frame, ctx);
			} else {
				cantp_logd("\033[0;33mCAN-TP Receiver:\033[0m "
						"ERROR: Not a correct Sate\n");
			}
		} break;
	}
}

void cantp_rx_task(void *arg)
{
	cantp_rxtx_status_t *ctx;
	ctx = (cantp_rxtx_status_t *)arg;

	cantp_can_frame_t rx_frame;
	while (1) {
		cantp_can_rx(&rx_frame, 0);
		//TODO: follow the Receiver and Sender status to multiplex the received frame
		//First we need to understand the type of the frame so that we can filter it
		//and direct it to Sender or Receiver

		cantp_canrx_cb(rx_frame.id, rx_frame.idt, rx_frame.dlc,
												rx_frame.data_u8, ctx);
	}

}

void cantp_sndr_task(void *arg)
{
	cantp_rxtx_status_t *ctx;
	ctx = (cantp_rxtx_status_t *)arg;

	cantp_logi("CANTP Task Start\n"); fflush(0);
	while (1) {
		switch (cantp_sndr_get_state(ctx)) {
			case CANTP_STATE_IDLE: {

				cantp_sndr_wait_state_change(ctx, 0);
			} break;
		case CANTP_STATE_SF_SENDING:
		case CANTP_STATE_FF_SENDING:
		case CANTP_STATE_CF_SENDING: {
				if (cantp_sndr_wait_tx_done(ctx, 1000 * CANTP_N_AS_TIMER_MS) == 0) {
					cantp_cantx_confirm_cb(ctx);
				}
			} break;
//		case CANTP_STATE_CF_SENDING: {
//				if (cantp_sndr_wait_tx_done(ctx, 1000 * CANTP_N_AS_TIMER_MS) == 0) {
//					cantp_cantx_confirm_cb(ctx);
//				}
//			} break;
		} //switch (cantp_get_sndr_state(ctx))
	}
}

void cantp_send(	cantp_rxtx_status_t *ctx,
					uint32_t id,
					uint8_t idt,
					uint8_t *data,
					uint16_t len)
{
	cantp_frame_t txframe = { 0 };

	cantp_logv("\033[0;36mCAN-TP Sender Sending: \033[0m ");
	for (uint16_t i=0; i < len; i++) {
		cantp_logv("0x%02x ", data[i]);
	}
	cantp_logv("\n"); fflush(0);

	cantp_logi("\033[0;36mCAN-TP Sender: "
			"\033[0;32m(S1.1)Sending from\033[0m ID=0x%06x IDt=%d ", id, idt);

	if (len <= CANTP_SF_NUM_DATA_BYTES) {
		//sending Single Frame
		txframe.sf.len = len;
		txframe.n_pci_t = CANTP_SINGLE_FRAME;
		for (uint8_t i = 0; i < len; i++) {
			txframe.sf.d[i] = data[i];
		}
		print_cantp_frame(txframe);
		cantp_logd("\t\033[0;35mCAN-TP Sender: \033[0m");
		cantp_timer_start(ctx->sndr.timer, "N_As", 1000 * CANTP_N_AS_TIMER_MS);
		cantp_sndr_set_state(ctx, CANTP_STATE_SF_SENDING);
		cantp_can_tx_nb(id, idt, /*len + 1*/8, txframe.u8);
		//wait for transmission to be confirmed cantp_cantx_confirm_cb()
	} else {
		//we need to send segmented data
		//so first we are sending First Frame
		ctx->params->id = id;
		ctx->params->idt = idt;
		ctx->sndr.len = len;
		ctx->sndr.data = data;
		txframe.n_pci_t = CANTP_FIRST_FRAME;
		cantp_ff_len_set(&txframe, len);
		for (uint8_t i = 0; i < CANTP_FF_NUM_DATA_BYTES; i++) {
			txframe.ff.d[i] = data[i];
		}
		print_cantp_frame(txframe);

		cantp_logd("\t\033[0;36mCAN-TP Sender:\033[0m (S1.2)");
		if (cantp_timer_start(ctx->sndr.timer, "N_As",
											1000 * CANTP_N_AS_TIMER_MS) < 0) {
		}; fflush(0);
		cantp_sndr_set_state(ctx, CANTP_STATE_FF_SENDING);

		cantp_logd("\t\033[0;36mCAN-TP Sender:\033[0m (S1.3)Sending...\n"); fflush(0);
		cantp_can_tx_nb(id, idt, 8, txframe.u8);
		cantp_logd("\t\033[0;36mCAN-TP Sender:\033[0m Waiting TX to be confirmed...\n"); fflush(0);
		//wait for transmission to be confirmed cantp_cantx_confirm_cb()
	}
}
