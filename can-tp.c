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

void cantp_tx_timer_cb(cantp_rxtx_status_t *ctx)
{
	printf("CAN-TP cantp_tx_timer_cb: ");
	//stopping N_As timer
	cantp_timer_stop(ctx->timer);
	switch (ctx->state) {
	case CANTP_STATE_SF_SENDING: {
//			//stopping N_As timer
//			cantp_timer_stop(ctx->timer);
			ctx->state = CANTP_STATE_IDLE;
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_A);
		}
		break;
	case CANTP_STATE_FF_SENT: {
			//stopping N_As timer
//			cantp_timer_stop(ctx->timer);
			ctx->state = CANTP_STATE_IDLE;
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_A);
		}
		break;
	case CANTP_STATE_FF_FC_WAIT: {
			//stopping N_Bs timer
//			cantp_timer_stop(ctx->timer);
			ctx->state = CANTP_STATE_IDLE;
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_Bs);
		}
		break;
	} //switch (ctx->tx_state.state)

}

void cantp_cantx_confirm_cb(cantp_rxtx_status_t *ctx)
{
	//The Sender transmits only Single Frame, First Frame and Consecutive Frames
	//so only this three types of frames can receive a transmit confirmation
	printf("\tTransmission Confirmed (PID=%d) State: \033[1;33m%s\033[0m\n",
			getpid(), cantp_state_enum_str[ctx->state]); fflush(0);
#if 1
	switch(ctx->state) {
	case CANTP_STATE_SF_SENDING: {
			cantp_timer_stop(ctx->timer);
			cantp_result_cb(CANTP_RESULT_N_OK);
		} break;
	case CANTP_STATE_FF_SENDING: {
			//First Frame is sent only from the Sender peer
			//We received a confirmation from data link layer
			//that the FF has being acknowledged (sent)
			//ISO 15765-2:2016 Figure 11 step(Key) 2
			ctx->state = CANTP_STATE_FF_SENT;
			printf("\033[0;35m\tCAN-TP Sender: "
						"Received TX confirmation of FF\033[0m\n"); fflush(0);

			//so we are stopping N_As timer
			cantp_timer_log("\033[0;35m\tCAN-TP Sender: \033[0m");
			cantp_timer_stop(ctx->timer); fflush(0);

			//starting new timer (N_Bs) for receiving Flow Control frame
			cantp_timer_log("\033[0;35m\tCAN-TP Sender: \033[0m");
			cantp_timer_start(ctx->timer, "N_Bs", 1000 * CANTP_N_BS_TIMER_MS);
			fflush(0);

			//Setting the sequence number so that the next transmission
			//of the Consecutive Frame start with 1
			ctx->sn = 1;
			ctx->index += CANTP_FF_NUM_DATA_BYTES;
			//Now Sender should wait for Flow Control frame
		} break;
	case CANTP_STATE_CF_SENT:
	case CANTP_STATE_CF_SENDING: {
			//Sender has received a confirmation for transmission of a CF
			printf("\033[0;35m\tCAN-TP Sender: "
								"Received TX confirmation of CF\033[0m\n");
			//stopping N_As timer
			cantp_timer_log("\033[0;35m\tCAN-TP Sender: \033[0m");
			cantp_timer_stop(ctx->timer); fflush(0);

			//Checking if it is Last Consecutive Frame
			int remaining = ctx->len - ctx->index;
			if (remaining < 0) {
				printf("\033[0;35m\tCAN-TP Sender: \033[0m "
						"Error: Wrong number of remaining bytes %d", remaining);
				//TODO: Error handling
				return;
			} else if (remaining == 0) {
				//the last Consecutive frame (for which we just received the
				//transmission confirmation) were the Last Consecutive Frame
				ctx->state = CANTP_STATE_TX_DONE;
				cantp_sndr_tx_done_cb();
				break;
			}

			ctx->sn ++;
			if (ctx->sn > 0x0f) {
				ctx->sn = 0;
			}

			//check if Block Size (BS) parameter form the Receiver is > 0
			//that could mean the Receiver will expect BS counts of
			//Consecutive Frames and then (the Receiver) will have to send
			//a Flow Control frame
			if (ctx->bs > 0) {
				ctx->bl_index++;
				if (ctx->bl_index > ctx->bs) {
					ctx->bl_index = 0;
					//We should wait for Flow Control Frame from the Receiver
					printf("\033[0;35m\tCAN-TP Sender: "
							"Waiting for Control Frame from Receiver\033[0m\n");
					cantp_timer_log("\033[0;35m\tCAN-TP Sender: \033[0m");
					cantp_timer_start(ctx->timer, "N_Bs",
										1000 * CANTP_N_BS_TIMER_MS); fflush(0);
					ctx->state = CANTP_STATE_CF_SENT;
					break;
				}
			}
			ctx->state = CANTP_STATE_CF_SENT;

//			printf("\033[0;35mCAN-TP Sender: "
//									"Sending next Consecutive Frame\033[0m\n");
			//starting timer N_Cs
			cantp_timer_log("\033[0;35m\tCAN-TP Sender: \033[0m");
			cantp_timer_start(ctx->timer, "N_Cs",
									1000 * CANTP_N_CS_TIMER_MS); fflush(0);

			uint8_t n_to_send; //Number of bytes to send with this CAN frame
			if (remaining > CANTP_CF_NUM_DATA_BYTES) {
				n_to_send = CANTP_CF_NUM_DATA_BYTES;
			} else {
				n_to_send = remaining;
			}

			//Now should prepare to send the next Consecutive Frame
			cantp_frame_t tx_frame = { 0 };
			tx_frame.n_pci_t = CANTP_CONSEC_FRAME;
			tx_frame.cf.sn = ctx->sn;

			//copy the data to CAN tx_frame
			for (uint8_t i=0; i < n_to_send; i++) {
				tx_frame.cf.d[i] = ctx->data[ctx->index++];
			}

			//Stopping timer N_Cs
			cantp_timer_log("\033[0;35m\tCAN-TP Sender: \033[0m");
			cantp_timer_stop(ctx->timer); fflush(0);

			//Starting timer N_As
			cantp_timer_log("\033[0;35m\tCAN-TP Sender: \033[0m");
			cantp_timer_start(ctx->timer, "N_As", 1000 * CANTP_N_AS_TIMER_MS);
			fflush(0);

			printf("\033[0;35mCAN-TP Sender: Sending from\033[0m ID=0x%06x IDt=%d ",
					ctx->id, ctx->idt);
			print_cantp_frame(tx_frame); fflush(0);

			cantp_can_tx_nb(ctx->id, ctx->idt, 8, tx_frame.u8);

			//Checking the number of remaining bytes to send with
			//the next Consecutive frame
//			int rem_after_send = ctx->len - ctx->index;
//			if (rem_after_send > 0) {
//
//			}
		} break;
	}
#endif
}

void cantp_rx_timer_cb(cantp_rxtx_status_t *ctx)
{
	cantp_timer_log("\033[0;33mCAN-TP Receiver: \033[0m");
	cantp_timer_stop(ctx->timer);
	switch (ctx->state) {
	case CANTP_STATE_FF_RCVD: {
			//cantp_rcvd_ff_cb() took too long to return
			printf("\033[0;33mCAN-TP Receiver: \033[0m");
			cantp_result_cb(CANTP_RESULT_N_TIMEOUT_A);
		} break;
	}
}

static inline void cantp_send_cf(cantp_frame_t *rx_frame, cantp_rxtx_status_t *ctx)
{
	//The Consecutive frame is send only from the Sender side and
	//only after a Flow Control frame or other Consecutive Frame

	//Stopping N_Cs timer
	cantp_timer_log("\033[0;35m\tCAN-TP Sender:\033[0m ");
	cantp_timer_stop(ctx->timer); fflush(0);

	//starting timer A_Cs
	cantp_timer_log("\033[0;35m\tCAN-TP Sender:\033[0m ");
	cantp_timer_start(ctx->timer, "N_As", 1000 * CANTP_N_CS_TIMER_MS);

	//Checking if it will be the Last Consecutive Frame
	int remaining = ctx->len - ctx->index;
	if (remaining <= 0) {
		printf("\033[0;35m\tCAN-TP Sender: \033[0m "
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
	tx_frame.cf.sn = ctx->sn;

	//copy the data to CAN tx_frame
	for (uint8_t i=0; i < n_to_send; i++) {
		tx_frame.cf.d[i] = ctx->data[ctx->index++];
	}

	ctx->state = CANTP_STATE_CF_SENDING;
	printf("\033[0;35mCAN-TP Sender: Sending from\033[0m ID=0x%06x IDt=%d ",
			ctx->id, ctx->idt);
	print_cantp_frame(tx_frame); fflush(0);

	cantp_can_tx_nb(ctx->id, ctx->idt, 8, tx_frame.u8);
	//wait for transmission to be confirmed cantp_cantx_confirm_cb()
}

static inline void cantp_rx_first_frame(uint32_t id,
					uint8_t idt,
					uint8_t dlc,
					cantp_frame_t *cantp_rx_frame,
					cantp_rxtx_status_t *ctx)
{
	cantp_frame_t rcvr_tx_frame = { 0 };

	//Starting N_Br timer
	cantp_timer_log("\033[0;33m\tCAN-TP Receiver: \033[0m");
	cantp_timer_start(ctx->timer, "N_Br", 1000 * CANTP_N_BR_TIMER_MS); fflush(0);

	//update some connection parameters into Receiver state
	//Actually the FF carries only CAN-TP message length information
	ctx->peer_id = id;//TODO: I still don't know what to do with this
				//so for now I am just passing it to the session layer
	ctx->len = cantp_ff_len_get(cantp_rx_frame);
	ctx->index = 0;
	ctx->bl_index = 0;

	//Calling Link Layer to inform it for a coming CAN-TP segmented message
	if (cantp_rcvr_rx_ff_cb(id, idt, &ctx->data, ctx->len) < 0) {
		//Stopping timer N_Br
		cantp_timer_stop(ctx->timer);
		ctx->state = CANTP_STATE_IDLE;
		//TODO: Error checks of both Sender and Receiver sides
		//Maybe the receiver should just ignore that CAN Frame in
		//which is not interested
		return;
	}
	if (ctx->data == NULL) {
		printf("\033[0;33m\tCAN-TP Receiver: "
				"Error: No buffer allocated from Session Layer\033[0m");
		return;
	}

	printf("\033[0;33m\tCAN-TP Receiver: Copying data to context buffer: \033[0m");
	for (uint8_t i=0; i < CANTP_FF_NUM_DATA_BYTES; i++) {
		ctx->data[ctx->index++] = cantp_rx_frame->ff.d[i];
		printf(" 0x%02x", cantp_rx_frame->ff.d[i]);
	}
	printf("\n"); fflush(0);

	//The Session Layer Receiver accepts this message
	ctx->state = CANTP_STATE_FF_RCVD;

	ctx->index = CANTP_FF_NUM_DATA_BYTES;

	//Stopping timer N_Br. This is the time of processing cantp_rcvd_ff_cb()
	cantp_timer_log("\033[0;33m\tCAN-TP Receiver: \033[0m");
	cantp_timer_stop(ctx->timer); fflush(0);

	//Starting timer N_Ar
	cantp_timer_log("\033[0;33m\tCAN-TP Receiver: \033[0m");
	cantp_timer_start(ctx->timer, "N_Ar", 1000 * CANTP_N_AR_TIMER_MS); fflush(0);

	rcvr_tx_frame.n_pci_t = CANTP_FLOW_CONTROLL;
	rcvr_tx_frame.fc.bs = ctx->bs;
	rcvr_tx_frame.fc.st = ctx->st;
	rcvr_tx_frame.fc.fs = CANTP_FC_FLOW_STATUS_CTS;
	ctx->state = CANTP_STATE_FC_SENDING;

	printf("\033[0;33m\tCAN-TP Receiver: Sending \033[0m");
	print_cantp_frame(rcvr_tx_frame);

	//Sending FC frame
	if (cantp_can_tx(ctx->id, ctx->idt, 8, rcvr_tx_frame.u8,
									1000 * CANTP_N_AR_TIMER_MS) < 0) {
		//Abort message reception and issue N_USData.indication
		//with <N_Result> = N_TIMEOUT_A
		cantp_result_cb(CANTP_RESULT_N_TIMEOUT_A);
	}
	ctx->state = CANTP_STATE_FC_SENT;

	//Stopping timer N_Ar
	cantp_timer_log("\033[0;33m\tCAN-TP Receiver: \033[0m");
	cantp_timer_stop(ctx->timer); fflush(0);
	//Starting N_Cr
	cantp_timer_log("\033[0;33m\tCAN-TP Receiver: \033[0m");
	cantp_timer_start(ctx->timer, "N_Cr", 1000 * CANTP_N_CR_TIMER_MS);
	//Now the Receiver should wait for first Consecutive Frame

}

static inline void cantp_rx_flow_control_frame(uint32_t id,
					uint8_t idt,
					uint8_t dlc,
					cantp_frame_t *cantp_rx_frame,
					cantp_rxtx_status_t *ctx)
{
	cantp_frame_t rcvr_tx_frame = { 0 };

	//Stopping N_Bs timer
	cantp_timer_log("\033[0;35m\tCAN-TP Sender:\033[0m ");
	cantp_timer_stop(ctx->timer); fflush(0);

	//starting timer N_Cs
	cantp_timer_log("\033[0;35m\tCAN-TP Sender:\033[0m ");
	cantp_timer_start(ctx->timer, "N_Cs", 1000 * CANTP_N_CS_TIMER_MS);

	//Saving connection parameters sent from the Receiver
	ctx->peer_id = id;	//Saving the peer (Receiver) ID. The IDT should
						//be same type as ours (Sender)
	ctx->bs = cantp_rx_frame->fc.bs;
	ctx->st = cantp_rx_frame->fc.st;
	if (ctx->st < 0x80) {
		ctx->st_timer_us = 1000 * ctx->st;
	} else if ((ctx->st >= 0xf1) && (ctx->st <= 0xf9)) {
		ctx->st_timer_us = 100 * (ctx->st - 0xf0);
	} else {
		printf("\033[0;31mERROR:\033[0m: incompatible value of STmin\n");
		//TODO: Error check !!!
		return;
	}
	switch (cantp_rx_frame->fc.fs) {
	case CANTP_FC_FLOW_STATUS_CTS: {
			//Continue to send
			if (ctx->st == 0) {
				cantp_send_cf(cantp_rx_frame, ctx);
			} else {
				//TODO: We need to implement here STmin delay if STmin > 0
			}

		} break;
	case CANTP_FC_FLOW_STATUS_WAIT: {

		} break;
	case CANTP_FC_FLOW_STATUS_OVF: {

		} break;
	}

}

static inline void cantp_rcvr_rx_consecutive_frame(uint32_t id,
					uint8_t idt,
					uint8_t dlc,
					cantp_frame_t *cantp_rx_frame,
					cantp_rxtx_status_t *ctx)
{
	cantp_frame_t rcvr_tx_frame = { 0 };

	//Checking if Consecutive frame is addressed to us
	if (ctx->peer_id != id) {
		printf("\033[0;33m\tCAN-TP Receiver:\033[0m "
				"Ignoring frame from ID=0x%06x expected was ID=0x%06x",
				id, ctx->peer_id);
		return;
	}
	printf("\033[0;33m\tCAN-TP Receiver: "
			"The received ID=0x%06x is correct\033[0m\n", id);

	//Stopping N_Cr timer
	cantp_timer_log("\033[0;33m\tCAN-TP Receiver: \033[0m");
	cantp_timer_stop(ctx->timer);

	//We are not starting the next timer here because we later
	ctx->sn ++;
	if (ctx->sn > 0x0f) {
		ctx->sn = 0;
	}
	if (ctx->sn != cantp_rx_frame->cf.sn) {
		printf("\033[0;33m\tCAN-TP Receiver: "
				"\033[0;31mError: The received SN=%d doesn't match local %d\033[0m\n",
				ctx->sn, cantp_rx_frame->cf.sn);
		//TODO: Error handling
		return;
	}
	printf("\033[0;33m\tCAN-TP Receiver: Sequence number %x is correct.\033[0m\n",
			cantp_rx_frame->cf.sn);

	//Checking if it is Last Consecutive Frame
	int remaining = ctx->len - ctx->index;
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
		ctx->data[ctx->index++] = cantp_rx_frame->cf.d[i];
		printf(" 0x%02x", cantp_rx_frame->cf.d[i]);
	}
	printf("\n"); fflush(0);

	if (remaining <= CANTP_CF_NUM_DATA_BYTES) {
		ctx->state = CANTP_STATE_TX_DONE;
		cantp_received_cb(ctx, ctx->id, ctx->idt, ctx->data, ctx->len);
		return;
	}

	//Starting N_Cr timer again
	cantp_timer_log("\033[0;33m\tCAN-TP Receiver: \033[0m");
	cantp_timer_start(ctx->timer, "N_Cr", 1000 * CANTP_N_CR_TIMER_MS); fflush(0);

	//TODO: Maybe need to check (ask Session Layer) for sending
	//a Flow Control Wait frame here. There is no such call in
	//ISO 15765-2:2016 : Figure 11
	//Maybe this "Waits" should be implemented based on global timing
	//parameters of the Receiver which defines times higher than STmin
	//and N_Cr

	//Checking if we (Receiver) need to send Flow Control frame
	if (ctx->bs > 0) { //First Checking if we need to count blocks at all
		//TODO: Implement this in Sender side too !!!!!
		ctx->bl_index ++;
		if (ctx->bl_index >= ctx->bs) {
			ctx->bl_index = 0;
			//Starting N_Br timer
			cantp_timer_log("\033[0;33mCAN-TP Receiver: \033[0m");
			cantp_timer_start(ctx->timer, "N_Br", 1000 * CANTP_N_BR_TIMER_MS);
			rcvr_tx_frame.fc.bs = ctx->bs;
			rcvr_tx_frame.fc.st = ctx->st;
			rcvr_tx_frame.n_pci_t = CANTP_FLOW_CONTROLL;
			//TODO: Maybe here should be implemented a call to Session Layer
			//to inform it for a partial data reception
			rcvr_tx_frame.fc.fs = CANTP_FC_FLOW_STATUS_CTS;
			ctx->state = CANTP_STATE_FC_SENDING;

			printf("\033[0;33mCAN-TP Receiver: Sending \033[0m");
			print_cantp_frame(rcvr_tx_frame);

			//Sending FC frame
			if (cantp_can_tx(ctx->id, ctx->idt, 8, rcvr_tx_frame.u8,
											1000 * CANTP_N_AR_TIMER_MS) < 0) {
				//Abort message reception and issue N_USData.indication
				//with <N_Result> = N_TIMEOUT_A
				cantp_result_cb(CANTP_RESULT_N_TIMEOUT_A);
			}
			ctx->state = CANTP_STATE_FC_SENT;

			//Stopping timer N_Ar
			cantp_timer_log("\033[0;33mCAN-TP Receiver: \033[0m");
			cantp_timer_stop(ctx->timer); fflush(0);

			//Starting N_Cr
			cantp_timer_log("\033[0;33mCAN-TP Receiver: \033[0m");
			cantp_timer_start(ctx->timer, "N_Cr", 1000 * CANTP_N_CR_TIMER_MS);

			//Now the Receiver should wait for a Consecutive Frame
			//or other Flow Control frame
			return;
		}
	}


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
			printf("\033[0;35mCAN-TP Sender: Received from\033[0m "
					"ID=0x%06x IDt=%d DLC=%d ", id, idt, dlc);
			print_cantp_frame(rx_frame); fflush(0);
			//present the received data to the Session Layer
			//(Receiver N_USData.ind)
			cantp_received_cb(ctx, id, idt,
					rx_frame.sf.d, rx_frame.sf.len);
		} break;
	case CANTP_FIRST_FRAME: {
			//First Frame can be received only from the Receiver side
			printf("\033[0;33mCAN-TP Receiver: Received from\033[0m "
					"ID=0x%06x IDt=%d DLC=%d ", id, idt, dlc);
			print_cantp_frame(rx_frame); fflush(0);
			printf("\033[0;33m\tCAN-TP Receiver: State: \033[1;33m%s\033[0m\n",
											cantp_state_enum_str[ctx->state]);

			//The receiver can accept a FF only if it is in IDLE state
			//i.e. not receiving another CAN-TP message
			if (ctx->state == CANTP_STATE_IDLE) {
				cantp_rx_first_frame(id, idt, dlc, &rx_frame, ctx);
			} else {
				printf("\033[0;33m\tCAN-TP Receiver:\033[0m "
						"ERROR: Not a correct Sate\n");
			}
		} break;
	case CANTP_FLOW_CONTROLL: {
			//Flow Control frame can be received only from the Sender side
			printf("\033[0;35mCAN-TP Sender: Received from\033[0m "
					"ID=0x%06x IDt=%d DLC=%d ", id, idt, dlc);
			print_cantp_frame(rx_frame); fflush(0);
			printf("\033[0;35m\tCAN-TP Sender: State: \033[1;33m%s\033[0m\n",
											cantp_state_enum_str[ctx->state]);

			if (ctx->state == CANTP_STATE_FF_SENT) {
				cantp_rx_flow_control_frame(id, idt, dlc, &rx_frame, ctx);
			} else {
				printf("\033[0;35m\tCAN-TP Sender:\033[0m "
						"ERROR: Not in a correct State\n");
			}
		} break;
	case CANTP_CONSEC_FRAME: {
			//Consecutive Frame can be received only from the Receiver
			printf("\033[0;33mCAN-TP Receiver: Received from\033[0m "
					"ID=0x%06x IDt=%d DLC=%d ", id, idt, dlc);
			print_cantp_frame(rx_frame); fflush(0);
			printf("\033[0;33m\tCAN-TP Receiver: State: \033[1;33m%s\033[0m\n",
											cantp_state_enum_str[ctx->state]);
			if (ctx->state == CANTP_STATE_FC_SENT) {
				cantp_rcvr_rx_consecutive_frame(id, idt, dlc, &rx_frame, ctx);
			}else {
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
//	printf("\033[0;35mCAN-TP Sender:\033[0m "
//			"Sending ");
	printf("\033[0;35mCAN-TP Sender:\033[0m "
			"Sending from ID=0x%06x IDt=%d ", id, idt);


	if (len <= CANTP_SF_NUM_DATA_BYTES) {
		//sending Single Frame
		txframe.sf.len = len;
		txframe.n_pci_t = CANTP_SINGLE_FRAME;
		for (uint8_t i = 0; i < len; i++) {
			txframe.sf.d[i] = data[i];
		}
		print_cantp_frame(txframe);
		printf("\033[0;35m\nCAN-TP Sender: \033[0m");
		if (cantp_timer_start(ctx->timer, "N_As",
											1000 * CANTP_N_AS_TIMER_MS) < 0) {
			res = -1;
		}; fflush(0);
//		printf("\033[0;35mCAN-TP Sender: "
//				"Sending from ID=0x%06x IDt=%d DLC=%d \033[0m ", id, idt, len + 1);
//		print_cantp_frame(txframe); fflush(0);
		ctx->state = CANTP_STATE_SF_SENDING;
		cantp_can_tx_nb(id, idt, len + 1, txframe.u8);
	} else {
		//we need to send segmented data
		//so first we are sending First Frame
		ctx->id = id;
		ctx->idt = idt;
		ctx->len = len;
		ctx->data = data;
		txframe.n_pci_t = CANTP_FIRST_FRAME;
		cantp_ff_len_set(&txframe, len);
		for (uint8_t i = 0; i < CANTP_FF_NUM_DATA_BYTES; i++) {
			txframe.ff.d[i] = data[i];
		}
		print_cantp_frame(txframe);
		printf("\033[0;35m\tCAN-TP Sender: \033[0m");
		if (cantp_timer_start(ctx->timer, "N_As",
											1000 * CANTP_N_AS_TIMER_MS) < 0) {
			res = -1;
		}; fflush(0);
		ctx->state = CANTP_STATE_FF_SENDING;
		cantp_can_tx_nb(id, idt, 8, txframe.u8);
	}
	return res;
}


