/*
 * cantp_test.c
 *
 *  Created on: May 9, 2022
 *      Author: refo
 *
 */

#include <stdio.h>		//printf()
#include <stdlib.h>		//exit(), malloc(), free()
#include <stdatomic.h>
#include <sys/shm.h>	//shmat(), IPC_RMID
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <semaphore.h>	//sem_open(), sem_destroy(), sem_wait()..
#include <fcntl.h>		//O_CREAT, O_EXEC
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include "can-tp.h"
#include "cbtimer_lin.h"
#include "fake_can_linux.h"
#include "cantp_glue_lin.h"

enum cantp_result_status_e {
	CANTP_RESULT_WAITING = 0,
	CANTP_RESULT_RECEIVED
};

static const char *cantp_result_enum_str[] = {
		FOREACH_CANTP_RESULT(GENERATE_STRING)
};

typedef struct {
	sem_t sem;
	int i;
} semint_t;

semint_t *sndr_wait_rcvr_sem;

void fake_cantx_confirm_cb(void *params)
{
	cantp_cantx_confirm_cb((cantp_rxtx_status_t *)params);
}

void fake_canrx_cb(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data, void *params)
{
	cantp_canrx_cb(id, idt, dlc, data, params);
}

void cantp_result_cb(int result)
{
	printf("CAN-TP: Received result: ");
	if (result == CANTP_RESULT_N_OK) {
		printf("\033[0;32m");
	} else {
		printf("\033[0;31m");
	}
	printf("%s\033[0m\n", cantp_result_enum_str[result]);

	sndr_wait_rcvr_sem->i = 1;
	msync(&sndr_wait_rcvr_sem->sem, sizeof(size_t), MS_SYNC);
	sem_post(&sndr_wait_rcvr_sem->sem);
}

void cantp_received_cb(cantp_rxtx_status_t *ctx,
					uint32_t id, uint8_t idt, uint8_t *data, uint8_t len)
{
	printf("\033[0;33mCAN-SL Receiver Received "
			"from ID=0x%06x IDT=%d :\033[0m ", id, idt);
	for (uint16_t i = 0; i < len; i++) {
		printf("0x%02x ", data[i]);
	}
	printf("\n"); fflush(0);
}

void cantp_rcvd_ff_cb(cantp_rxtx_status_t *ctx,
			uint32_t id, uint8_t idt, uint8_t *data, uint16_t len)
{
	printf("\033[0;36mCAN-SL Receiver: Received First Frame "
			"from ID=0x%06x IDT=%d CAN-TP Message LEN=%d bytes:\033[0m \n",
			id, idt, ctx->len);
	for (uint16_t i; i < len; i++) {
		printf("0x%02x ", data[i]);
	}
	putchar('\n');
	fflush(0);

}

static void cantp_rx_t_cb(cbtimer_t *tim)
{
	cantp_rxtx_status_t *ctx = (cantp_rxtx_status_t *)(tim->cb_params);
	cantp_rx_timer_cb(ctx);
}

void receiver_task(uint32_t id, uint8_t idt, uint8_t rx_bs, uint8_t rx_st)
{
	printf("\033[0;33mInitializing the Receiver side\033[0m\n");fflush(0);
	static cantp_rxtx_status_t ctp_rcvr_state = { 0 };
	static cbtimer_t ctp_rcvr_timer;
	long cdrv_rcvr_tx_delay = 1000;

	ctp_rcvr_state.id = id;
	ctp_rcvr_state.idt = idt;

	fake_can_rcvr_init(cdrv_rcvr_tx_delay, "\033[0;34mCAN-LL Receiver\033[0m",
													&ctp_rcvr_state);

	cantp_rx_params_init(&ctp_rcvr_state, rx_bs, rx_st);
	cantp_set_timer_ptr(&ctp_rcvr_timer, &ctp_rcvr_state);
	cbtimer_set_cb(&ctp_rcvr_timer, cantp_rx_t_cb, &ctp_rcvr_state);

	sndr_wait_rcvr_sem->i = 1;
	msync(&sndr_wait_rcvr_sem->sem, sizeof(size_t), MS_SYNC);
	sem_post(&sndr_wait_rcvr_sem->sem);

	fake_can_rx_task(&ctp_rcvr_state);
}

//There are two different sides of the test:
// - Sender side is one that stays at the left side of the
//ISO 15765-2:2016: Figure 11. It will send a test CAN-TP messages
//and receive FC indications, FF and CF confirmations as soon as the
//last CF confirmation has being received.
// - Receiver side the right side of the ISO 15765-2:2016: Figure 11.
//It runs on a separate task as it is a opposite pair of the CAN-TP
//messaging flow.
//Each pair has it's own context that includes their own state of
//either the CAN-TP (network/transport) layer and CAN driver (link) layer
//
//From the look of the CAN-TP side there will also have two different
//pairs:
// - Sender with it's own state "ctp_sndr_state" and
// - Receiver with "ctp_rcvr_state"

int main(int argc, char **argv)
{
	long cdrv_sndr_tx_delay = 1000;
	if (argc > 1) {
		cdrv_sndr_tx_delay = atoi(argv[1]);
		printf("candrv_tx_delay = %ldμs\n", cdrv_sndr_tx_delay);
	}

	sndr_wait_rcvr_sem = (semint_t*) mmap(NULL, sizeof(semint_t), PROT_READ | PROT_WRITE,
										MAP_ANONYMOUS | MAP_SHARED, 0, 0);
	if (sndr_wait_rcvr_sem == MAP_FAILED) {
		printf("Couldn't initialize semaphore mapping\n");
		return EXIT_FAILURE;
	}

	sem_init(&sndr_wait_rcvr_sem->sem, 1, 0);
	sndr_wait_rcvr_sem->i = 0;

	printf("Initializing the Sender side\n");
	static cantp_rxtx_status_t cantp_sndr_state;
	fake_can_init(cdrv_sndr_tx_delay, "\033[0;35mCAN-LL Sender\033[0m",
															&cantp_sndr_state);

	//Maybe is better to use threads instead of fork()
	//but someone can improve it if he wants
	pid_t pid = fork();
	if (pid == (pid_t) 0) {
		//This is the child process.
		printf("Receiver pid = %d\n", getpid());
		receiver_task(0xbbb, 0, 0, 0);
		printf("Receiver END\n"); fflush(0);
		return EXIT_SUCCESS;
	} else if (pid < (pid_t) 0) {
		fprintf(stderr, "Fork failed.\n");
		return EXIT_FAILURE;
	}
	printf("Sender pid = %d\n", getpid());
//	printf("pid = %d\n", pid);

	uint16_t dlen = 16;
	uint8_t *data = malloc(dlen);
	for (uint16_t i = 0; i < dlen; i++) {
		data[i] = (uint8_t)(0xff & i) + 1;
	}

	static cbtimer_t ctp_sndr_timer;
	cantp_set_timer_ptr(&ctp_sndr_timer, &cantp_sndr_state);
	cbtimer_set_cb(&ctp_sndr_timer, cantp_tx_t_cb, &cantp_sndr_state);

	//First wait for Receiver to be initialized
	sem_wait(&sndr_wait_rcvr_sem->sem);

	printf("------We can send now------\n");
	cantp_send(&cantp_sndr_state, 0xAAA, 0, data, dlen);

	sem_wait(&sndr_wait_rcvr_sem->sem);
//	kill(pid, SIGHUP);
	printf("EndMain\n");fflush(0);
	return 0;
}
