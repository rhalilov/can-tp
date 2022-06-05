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

#include "can-tp.h"

static const char *cantp_state_enum_str[] = {
		FOREACH_CANTP_STATE(GENERATE_STRING)
};

void cantp_sndr_timer_cb(cantp_rxtx_status_t *ctx)
{
	printf("CAN-TP Sender: State: \033[1;33m%s\033[0m ",
								cantp_state_enum_str[ctx->sndr.state]);
	//stopping N_As timer
	cantp_timer_stop(ctx->sndr.timer);
	switch (ctx->sndr.state) {
	case CANTP_STATE_SF_SENDING: {
			ctx->sndr.state = CANTP_STATE_IDLE;
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_As);
		}
		break;
	case CANTP_STATE_FF_SENT: {
			ctx->sndr.state = CANTP_STATE_IDLE;
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_Bs);
		}
		break;
	case CANTP_STATE_FF_FC_WAIT: {
			ctx->sndr.state = CANTP_STATE_IDLE;
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_Bs);
		}
		break;
	} //switch (ctx->tx_state.state)

}

void cantp_rcvr_timer_cb(cantp_rxtx_status_t *ctx)
{
	printf("CAN-TP Receiver: State: \033[1;33m%s\033[0m ",
								cantp_state_enum_str[ctx->rcvr.state]);
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
//	printf("cantp_send_cf_afrer_cf \n"); fflush(0);
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
	cantp_timer_log("\t\033[0;36mCAN-TP Sender:\033[0m (S7.1)");
	cantp_timer_stop(ctx->sndr.timer); fflush(0);

	//Starting timer N_As
	cantp_timer_log("\t\033[0;36mCAN-TP Sender:\033[0m (S7.2)");
	cantp_timer_start(ctx->sndr.timer, "N_As", 1000 * CANTP_N_AS_TIMER_MS);
	fflush(0);

	printf("\033[0;36mCAN-TP Sender: "
			"\033[0;32m(S7.3)Sending from\033[0m ID=0x%06x IDt=%d ",
			ctx->params->id, ctx->params->idt);
	print_cantp_frame(tx_frame); fflush(0);

	cantp_can_tx_nb(ctx->params->id, ctx->params->idt, 8, tx_frame.u8);
}

void cantp_cantx_confirm_cb(cantp_rxtx_status_t *ctx)
{
	//The Sender transmits only Single Frame, First Frame and Consecutive Frames
	//so only this three types of frames can receive a transmit confirmation
//	printf("\tTransmission Confirmed (PID=%d)State: \033[1;33m%s\033[0m\n",
//			getpid(), cantp_state_enum_str[ctx->sndr.state]); fflush(0);

	switch(ctx->sndr.state) {
	case CANTP_STATE_SF_SENDING: {
			cantp_timer_stop(ctx->sndr.timer);
			cantp_result_cb(CANTP_RESULT_N_OK);
		} break;
	case CANTP_STATE_FF_SENDING: {
			//First Frame is sent only from the Sender peer
			//We received a confirmation from data link layer
			//that the FF has being acknowledged (sent)
			//ISO 15765-2:2016 Figure 11 step(Key) 2
			ctx->sndr.state = CANTP_STATE_FF_SENT;
			printf("\t\033[0;36mCAN-TP Sender:\033[0m "
				"(S2.1)Received TX confirmation of First Frame\n"); fflush(0);

			//so we are stopping N_As timer
			cantp_timer_log("\t\033[0;36mCAN-TP Sender:\033[0m (S2.2)");
			cantp_timer_stop(ctx->sndr.timer); fflush(0);

			ctx->sndr.state = CANTP_STATE_FF_SENT;

			//starting new timer (N_Bs) for receiving Flow Control frame
			cantp_timer_log("\t\033[0;36mCAN-TP Sender:\033[0m (S2.3)");
			cantp_timer_start(ctx->sndr.timer, "N_Bs",
										1000 * CANTP_N_BS_TIMER_MS); fflush(0);

			//Setting the sequence number so that the next transmission
			//of the Consecutive Frame start with 1
			ctx->sndr.sn = 1;
			ctx->sndr.index += CANTP_FF_NUM_DATA_BYTES;
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
			printf("\t\033[0;36mCAN-TP Sender:\033[0m "
							"(S6.1)Received TX confirmation of Consecutive Frame\n");

			//stopping N_As timer
			cantp_timer_log("\t\033[0;36mCAN-TP Sender:\033[0m (S6.2)");
			cantp_timer_stop(ctx->sndr.timer); fflush(0);

			//Checking if it is Last Consecutive Frame
			int remaining = ctx->sndr.len - ctx->sndr.index;
			if (remaining < 0) {
				printf("\t\033[0;36mCAN-TP Sender: \033[0m "
						"Error: Wrong number of remaining bytes %d", remaining);
				//TODO: Error handling
				return;
			} else if (remaining == 0) {
				//the last Consecutive frame (for which we just received the
				//transmission confirmation) were the Last Consecutive Frame
				printf("\t\033[0;36mCAN-TP Sender:\033[0m (16.1)\n"); fflush(0);
				ctx->sndr.state = CANTP_STATE_TX_DONE;
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
				printf("\t\033[0;36mCAN-TP Sender:\033[0m "
							"BlockSize=%d block sent so far %d\n",
								ctx->sndr.bs_pair, ctx->sndr.bl_index); fflush(0);

				if (ctx->sndr.bl_index >= ctx->sndr.bs_pair) {
					ctx->sndr.bl_index = 0;
					//We should wait for Flow Control Frame from the Receiver
					printf("\t\033[0;36mCAN-TP Sender:\033[0m "
							"(8.1)Waiting for Flow Control Frame from Receiver\n");

					cantp_timer_log("\t\033[0;36mCAN-TP Sender:\033[0m (8.2)");
					cantp_timer_start(ctx->sndr.timer, "N_Bs",
										1000 * CANTP_N_BS_TIMER_MS); fflush(0);
					ctx->sndr.state = CANTP_STATE_CF_FC_WAIT;
					break;
				}
			}
			ctx->sndr.state = CANTP_STATE_CF_SENT;

//			printf("\033[0;36mCAN-TP Sender: "
//									"Sending next Consecutive Frame\033[0m\n");
			//starting timer N_Cs
			cantp_timer_log("\t\033[0;36mCAN-TP Sender:\033[0m (S6.3)");
			cantp_timer_start(ctx->sndr.timer, "N_Cs",
									1000 * CANTP_N_CS_TIMER_MS); fflush(0);

			if (ctx->sndr.st_tim_us > 0) {
				cantp_timer_log("\t\033[0;36mCAN-TP Sender:\033[0m (S6.4)"); fflush(0);
				cantp_usleep(ctx->sndr.st_tim_us);
			}
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
	cantp_timer_log("\t\033[0;36mCAN-TP Sender:\033[0m (S5.1)");
	cantp_timer_stop(ctx->sndr.timer); fflush(0);

	//starting timer N_As
	cantp_timer_log("\t\033[0;36mCAN-TP Sender:\033[0m (S5.2)");
	cantp_timer_start(ctx->sndr.timer, "N_As", 1000 * CANTP_N_CS_TIMER_MS); fflush(0);

	//Checking if it will be the Last Consecutive Frame
	int remaining = ctx->sndr.len - ctx->sndr.index;
	if (remaining <= 0) {
		printf("\t\033[0;36mCAN-TP Sender: \033[0m "
				"Error: Wrong number of remaining bytes %d", remaining);
		//TODO: Error handling
		return;
	}

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

	ctx->sndr.state = CANTP_STATE_CF_SENDING;
	printf("\033[0;36mCAN-TP Sender: \033[0;32m(S5.3)Sending from\033[0m ID=0x%06x IDt=%d ",
			ctx->params->id, ctx->params->idt);
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

	//Starting N_Br timer
	cantp_timer_log("\033[0;33m\tCAN-TP Receiver:\033[0m (R2.2)");
	cantp_timer_start(ctx->rcvr.timer, "N_Br", 1000 * CANTP_N_BR_TIMER_MS); fflush(0);

	//update some connection parameters into Receiver state
	//Actually the FF carries only CAN-TP message length information
	ctx->rcvr.id_pair = id;//TODO: I still don't know what to do with this
				//so for now I am just passing it to the session layer
	ctx->rcvr.len = cantp_ff_len_get(cantp_rx_frame);
	ctx->rcvr.index = 0;
	ctx->rcvr.bl_index = 0;

	//Calling Link Layer to inform it for a coming CAN-TP segmented message
	if (cantp_rcvr_rx_ff_cb(id, idt, &ctx->rcvr.data, ctx->rcvr.len) < 0) {
		//Stopping timer N_Br
		cantp_timer_stop(ctx->rcvr.timer);
		ctx->rcvr.state = CANTP_STATE_IDLE;
		//TODO: Error checks of both Sender and Receiver sides
		//Maybe the receiver should just ignore that CAN Frame in
		//which is not interested
		return;
	}
	if (ctx->rcvr.data == NULL) {
		printf("\033[0;33m\tCAN-TP Receiver: "
				"Error: No buffer allocated from Session Layer\033[0m");
		return;
	}

	printf("\033[0;33m\tCAN-TP Receiver: Copying data to context buffer: \033[0m");
	for (uint8_t i=0; i < CANTP_FF_NUM_DATA_BYTES; i++) {
		ctx->rcvr.data[ctx->rcvr.index++] = cantp_rx_frame->ff.d[i];
		printf(" 0x%02x", cantp_rx_frame->ff.d[i]);
	}
	printf("\n"); fflush(0);

	//The Session Layer Receiver accepts this message
	ctx->rcvr.state = CANTP_STATE_FF_RCVD;

	ctx->rcvr.index = CANTP_FF_NUM_DATA_BYTES;

	//Stopping timer N_Br. This is the time of processing cantp_rcvd_ff_cb()
	cantp_timer_log("\033[0;33m\tCAN-TP Receiver:\033[0m (R3.1)");
	cantp_timer_stop(ctx->rcvr.timer); fflush(0);

	//Starting timer N_Ar
	cantp_timer_log("\033[0;33m\tCAN-TP Receiver:\033[0m (R3.2)");
	cantp_timer_start(ctx->rcvr.timer, "N_Ar", 1000 * CANTP_N_AR_TIMER_MS); fflush(0);

	cantp_frame_t rcvr_tx_fc_frame = { 0 };

	rcvr_tx_fc_frame.n_pci_t = CANTP_FLOW_CONTROLL;
	rcvr_tx_fc_frame.fc.bs = ctx->params->block_size;
	rcvr_tx_fc_frame.fc.st = ctx->params->st_min;
	rcvr_tx_fc_frame.fc.fs = CANTP_FC_FLOW_STATUS_CTS;
	ctx->rcvr.state = CANTP_STATE_FC_SENDING;

	printf("\033[0;33mCAN-TP Receiver: \033[0;32m(R3.3)Sending \033[0m");
	print_cantp_frame(rcvr_tx_fc_frame);

	//Sending FC frame
	if (cantp_can_tx(ctx->params->id, ctx->params->idt, 8, rcvr_tx_fc_frame.u8,
									1000 * CANTP_N_AR_TIMER_MS) < 0) {
		//Abort message reception and issue N_USData.indication
		//with <N_Result> = N_TIMEOUT_A
		cantp_result_cb(CANTP_RESULT_N_TIMEOUT_As);
	}
	ctx->rcvr.state = CANTP_STATE_FC_SENT;
	cantp_timer_log("\033[0;33m\tCAN-TP Receiver:\033[0m"
			"(R4.1)TX Done \n"); fflush(0);

	//Stopping timer N_Ar
	cantp_timer_log("\033[0;33m\tCAN-TP Receiver:\033[0m (R4.2)");
	cantp_timer_stop(ctx->rcvr.timer); fflush(0);
	//Starting N_Cr
	cantp_timer_log("\033[0;33m\tCAN-TP Receiver:\033[0m (R4.3)");
	cantp_timer_start(ctx->rcvr.timer, "N_Cr", 1000 * CANTP_N_CR_TIMER_MS);
	//Now the Receiver should wait for first Consecutive Frame

}

static inline void sndr_rx_flow_control_frame(uint32_t id,
					uint8_t idt,
					uint8_t dlc,
					cantp_frame_t *cantp_rx_frame,
					cantp_rxtx_status_t *ctx)
{
	cantp_frame_t rcvr_tx_frame = { 0 };

	//Stopping N_Bs timer
	cantp_timer_log("\t\033[0;36mCAN-TP Sender:\033[0m (S4.2)");
	cantp_timer_stop(ctx->sndr.timer); fflush(0);

	//starting timer N_Cs
	cantp_timer_log("\t\033[0;36mCAN-TP Sender:\033[0m (S4.3)");
	cantp_timer_start(ctx->sndr.timer, "N_Cs", 1000 * CANTP_N_CS_TIMER_MS);

	//Saving connection parameters sent from the Receiver
	ctx->sndr.id_pair = id;	//Saving the peer (Receiver) ID. The IDT should
						//be same type as ours (Sender)
	ctx->sndr.bs_pair = cantp_rx_frame->fc.bs;
	ctx->sndr.st_pair = cantp_rx_frame->fc.st;
	if (ctx->sndr.st_pair < 0x80) {
		ctx->sndr.st_tim_us = 1000 * ctx->sndr.st_pair;
	} else if ((ctx->sndr.st_pair >= 0xf1) && (ctx->sndr.st_pair <= 0xf9)) {
		ctx->sndr.st_tim_us = 100 * (ctx->sndr.st_pair - 0xf0);
	} else {
		printf("\033[0;31mERROR:\033[0m: incompatible value of STmin\n");fflush(0);
		//TODO: Error check !!!
		return;
	}
	switch (cantp_rx_frame->fc.fs) {
	case CANTP_FC_FLOW_STATUS_CTS: {
			//Continue to send
			sndr_send_cf_afrer_fc(cantp_rx_frame, ctx);
		} break;
	case CANTP_FC_FLOW_STATUS_WAIT: {
			//TODO: Implementation of CANTP_FC_FLOW_STATUS_WAIT
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
		printf("\033[0;33mCAN-TP Receiver:\033[0m "
				"Ignoring frame from ID=0x%06x expected was ID=0x%06x",
				id, ctx->rcvr.id_pair);
		return;
	}

	//Stopping N_Cr timer
	cantp_timer_log("\t\033[0;33mCAN-TP Receiver:\033[0m (R6.2)");
	cantp_timer_stop(ctx->rcvr.timer);

	//We are not starting the next timer here because first we
	//need to decide is: are we waiting for next Consecutive Frame
	//or need to send Flow Control frame

	//Processing the received Consecutive Frame
	ctx->rcvr.sn ++;
	if (ctx->rcvr.sn > 0x0f) {
		ctx->rcvr.sn = 0;
	}
	if (ctx->rcvr.sn != cantp_rx_frame->cf.sn) {
		printf("\033[0;33m\tCAN-TP Receiver: "
				"\033[0;31mError: The received SN=%d doesn't match local %d\033[0m\n",
				ctx->rcvr.sn, cantp_rx_frame->cf.sn);
		//TODO: Error handling
		return;
	}
	printf("\033[0;33m\tCAN-TP Receiver: Sequence number %x is correct.\033[0m\n",
			cantp_rx_frame->cf.sn);

	//Checking if it is Last Consecutive Frame
	int remaining = ctx->rcvr.len - ctx->rcvr.index;
	if (remaining <= 0) {
		printf("\033[0;33m\tCAN-TP Receiver: \033[0m "
				"Error: Wrong number of remaining bytes %d", remaining);
		//TODO: Error handling
		return;
	}
	uint8_t bytes_rcvd = (remaining < CANTP_CF_NUM_DATA_BYTES)?
			remaining:CANTP_CF_NUM_DATA_BYTES;

	printf("\033[0;33m\tCAN-TP Receiver: "
			"Copying %d bytes to context buffer: \033[0m", bytes_rcvd);

	for (uint8_t i=0; i < bytes_rcvd; i++) {
		ctx->rcvr.data[ctx->rcvr.index++] = cantp_rx_frame->cf.d[i];
		printf(" 0x%02x", cantp_rx_frame->cf.d[i]);
	}
	printf("\n"); fflush(0);

	if (remaining <= CANTP_CF_NUM_DATA_BYTES) {
		ctx->rcvr.state = CANTP_STATE_TX_DONE;
		cantp_received_cb(ctx, ctx->params->id, ctx->params->idt,
												ctx->rcvr.data, ctx->rcvr.len);
		return;
	}

	//Checking if we (Receiver) need to send Flow Control frame or
	//wait for next Consecutive Frame from the Sender
	//First Checking if we need to count blocks at all
	if (ctx->params->block_size > 0) {
		ctx->sndr.bl_index ++;
		printf("\t\033[0;33mCAN-TP Receiver:\033[0m "
								"BlockSize=%d block received till now %d\n",
										ctx->params->block_size, ctx->sndr.bl_index); fflush(0);

		if (ctx->sndr.bl_index >= ctx->params->block_size) {
			ctx->sndr.bl_index = 0;
			//Starting N_Br timer
			cantp_timer_log("\033[0;33mCAN-TP Receiver:\033[0m (R8.3)");
			cantp_timer_start(ctx->rcvr.timer, "N_Br", 1000 * CANTP_N_BR_TIMER_MS);

			rcvr_tx_frame.fc.bs = ctx->params->block_size;
			rcvr_tx_frame.fc.st = ctx->params->st_min;
			rcvr_tx_frame.n_pci_t = CANTP_FLOW_CONTROLL;
			//TODO: Maybe here should be implemented a call to Session Layer
			//to inform it for a partial data reception
			rcvr_tx_frame.fc.fs = CANTP_FC_FLOW_STATUS_CTS;
			ctx->rcvr.state = CANTP_STATE_FC_SENDING;

			printf("\033[0;33mCAN-TP Receiver: (R9.2)Sending \033[0m");
			print_cantp_frame(rcvr_tx_frame);

			//Sending FC frame
			if (cantp_can_tx(ctx->params->id, ctx->params->idt, 8, rcvr_tx_frame.u8,
											1000 * CANTP_N_AR_TIMER_MS) < 0) {
				//Abort message reception and issue N_USData.indication
				//with <N_Result> = N_TIMEOUT_A
				cantp_result_cb(CANTP_RESULT_N_TIMEOUT_As);
			}
			ctx->rcvr.state = CANTP_STATE_FC_SENT;
			cantp_timer_log("\t\033[0;33mCAN-TP Receiver:\033[0m "
					"(R12.1)Received TX confirmationn of Flow Controll frame\n");

			//Stopping timer N_Ar
			cantp_timer_log("\t\033[0;33mCAN-TP Receiver:\033[0m (R12.2)");
			cantp_timer_stop(ctx->rcvr.timer); fflush(0);

			//Starting N_Cr
			cantp_timer_log("\t\033[0;33mCAN-TP Receiver:\033[0m (R12.3");
			cantp_timer_start(ctx->rcvr.timer, "N_Cr", 1000 * CANTP_N_CR_TIMER_MS);

			//Now the Receiver should wait for a Consecutive Frame
			//or other Flow Control frame
			return;
		}
	} //if (ctx->params->block_size > 0)

	//TODO: Maybe need to check (ask Session Layer) for sending
	//a Flow Control Wait frame here. There is no such call in
	//ISO 15765-2:2016 : Figure 11
	//Maybe this "Waits" should be implemented based on global timing
	//parameters of the Receiver which defines times higher than STmin
	//and N_Cr
	//We will do that only for testing purposes !!!
	//Checking if Wait Frame is enabled in the Receiver side
//	if (ctx->params.wft_max > 0) {
//		//Check if we exceeded the maximum number of Flow Control Wait frames
//		if (ctx->wft_cntr > ctx->params.wft_max) {
//			cantp_result_cb(CANTP_RESULT_N_WFT_OVRN);
//			return;
//		}
//		//The Flow Control Wait frame should be sent for not more than N_Br
//		//For a timer we will use the value of the STmin which will define
//		//timeout for not more than 900 milliseconds (to not exceed N_Br)
//		cantp_usleep(ctx->params.wft_tim_us);
//
//
//	}


	//We are waiting for next Consecutive Frame

	//Starting N_Cr timer again
	cantp_timer_log("\033[0;33m\tCAN-TP Receiver:\033[0m (R6.3)");
	cantp_timer_start(ctx->rcvr.timer, "N_Cr", 1000 * CANTP_N_CR_TIMER_MS); fflush(0);

}

void cantp_canrx_cb(uint32_t id,
					uint8_t idt,
					uint8_t dlc,
					uint8_t *data,
					cantp_rxtx_status_t *ctx)
{
	//TODO: We need to make some ID checks here

	//TODO: check validity of dlc
	cantp_frame_t rx_frame, rcvr_tx_frame = { 0 };
	for (uint8_t i=0; i < dlc; i++) {
		rx_frame.u8[i] = data[i];
	}

	switch (rx_frame.n_pci_t) {
	case CANTP_SINGLE_FRAME: {
			//Single Frame can be received only from the Receiver
			printf("\033[0;36mCAN-TP Sender: \033[1;33mReceived from\033[0m "
					"ID=0x%06x IDt=%d DLC=%d ", id, idt, dlc);
			print_cantp_frame(rx_frame); fflush(0);
			//present the received data to the Session Layer
			//(Receiver N_USData.ind)
			cantp_received_cb(ctx, id, idt,
					rx_frame.sf.d, rx_frame.sf.len);
		} break;
	case CANTP_FIRST_FRAME: {
			//First Frame can be received only from the Receiver side
			printf("\033[0;33mCAN-TP Receiver: \033[1;33m(R2.1)Received from\033[0m "
					"ID=0x%06x IDt=%d DLC=%d ", id, idt, dlc);
			print_cantp_frame(rx_frame); fflush(0);
			printf("\033[0;33m\tCAN-TP Receiver: State: \033[1;33m%s\033[0m\n",
											cantp_state_enum_str[ctx->rcvr.state]);

			//The receiver can accept a FF only if it is in IDLE state
			//i.e. not receiving another CAN-TP message
			if (ctx->rcvr.state == CANTP_STATE_IDLE) {
				rcvr_rx_first_frame(id, idt, dlc, &rx_frame, ctx);
			} else {
				printf("\033[0;33m\tCAN-TP Receiver:\033[0m "
						"ERROR: Not a correct Sate\n");
			}
		} break;
	case CANTP_FLOW_CONTROLL: {
			//Flow Control frame can be received only from the Sender side
			printf("\033[0;36mCAN-TP Sender: \033[1;33m(S4.1)Received from\033[0m "
					"ID=0x%06x IDt=%d DLC=%d ", id, idt, dlc);
			print_cantp_frame(rx_frame); fflush(0);

			printf("\t\033[0;36mCAN-TP Sender: State: \033[1;33m%s\033[0m\n",
											cantp_state_enum_str[ctx->sndr.state]);

			sndr_rx_flow_control_frame(id, idt, dlc, &rx_frame, ctx);

		} break;
	case CANTP_CONSEC_FRAME: {
			//Consecutive Frame can be received only from the Receiver
			printf("\033[0;33mCAN-TP Receiver: \033[1;33m(R6.1)Received from\033[0m "
					"ID=0x%06x IDt=%d DLC=%d ", id, idt, dlc);
			print_cantp_frame(rx_frame); fflush(0);
			printf("\033[0;33m\tCAN-TP Receiver: State: \033[1;33m%s\033[0m\n",
											cantp_state_enum_str[ctx->rcvr.state]);

			if (ctx->rcvr.state == CANTP_STATE_FC_SENT) {
				rcvr_rx_consecutive_frame(id, idt, dlc, &rx_frame, ctx);
			} else {
				printf("\033[0;33m\tCAN-TP Receiver:\033[0m "
						"ERROR: Not a correct Sate\n");
			}
		} break;
	}
}

int cantp_send	(cantp_rxtx_status_t *ctx,
				uint32_t id,
				uint8_t idt,
				uint8_t *data,
				uint16_t len)
{
	int res;
	cantp_frame_t txframe;
//	printf("\033[0;36mCAN-TP Sender:\033[0m "
//			"Sending ");
	printf("\033[0;36mCAN-TP Sender: "
			"\033[0;32m(S1.1)Sending from\033[0m ID=0x%06x IDt=%d ", id, idt);


	if (len <= CANTP_SF_NUM_DATA_BYTES) {
		//sending Single Frame
		txframe.sf.len = len;
		txframe.n_pci_t = CANTP_SINGLE_FRAME;
		for (uint8_t i = 0; i < len; i++) {
			txframe.sf.d[i] = data[i];
		}
		print_cantp_frame(txframe);
		cantp_timer_log("\033[0;35m\nCAN-TP Sender: \033[0m");
		if (cantp_timer_start(ctx->sndr.timer, "N_As",
											1000 * CANTP_N_AS_TIMER_MS) < 0) {
			res = -1;
		}; fflush(0);
//		printf("\033[0;36mCAN-TP Sender: "
//				"\033[0;32mSending from\033[0m ID=0x%06x IDt=%d DLC=%d ", id, idt, len + 1);
//		print_cantp_frame(txframe); fflush(0);
		ctx->sndr.state = CANTP_STATE_SF_SENDING;
		cantp_can_tx_nb(id, idt, len + 1, txframe.u8);
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

		cantp_timer_log("\t\033[0;36mCAN-TP Sender:\033[0m (S1.2)");
		if (cantp_timer_start(ctx->sndr.timer, "N_As",
											1000 * CANTP_N_AS_TIMER_MS) < 0) {
			res = -1;
		}; fflush(0);
		ctx->sndr.state = CANTP_STATE_FF_SENDING;

		printf("\t\033[0;36mCAN-TP Sender:\033[0m (S1.3)Sending...\n"); fflush(0);
		cantp_can_tx_nb(id, idt, 8, txframe.u8);
	}
	return res;
}


