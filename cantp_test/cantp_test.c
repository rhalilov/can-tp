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

sem_t *rcvr_end_sem, *rcvr_start_sem;

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

	msync(rcvr_end_sem, sizeof(size_t), MS_SYNC);
	sem_post(rcvr_end_sem);
}

void cantp_received_cb(cantp_rxtx_status_t *ctx,
					uint32_t id, uint8_t idt, uint8_t *data, uint8_t len)
{
	printf("\033[0;36mCAN-SL Receiver Received "
			"from ID=0x%06x IDT=%d :\033[0m ", id, idt);
	for (uint16_t i = 0; i < len; i++) {
		printf("0x%02x ", data[i]);
	}
	printf("\n"); fflush(0);
	//spend some time here
	usleep(100000);

	msync(rcvr_end_sem, sizeof(size_t), MS_SYNC);
	sem_post(rcvr_end_sem);
}

int cantp_rcvr_rx_ff_cb(uint32_t id, uint8_t idt, uint8_t **data, uint16_t len)
{
	printf("\033[0;35mCAN-SL Receiver: (R2.3)Received First Frame "
			"from ID=0x%06x IDT=%d CAN-TP Message LEN=%d\033[0m \n",
			id, idt, len); fflush(0);
	*data = malloc(len);
	if (*data == NULL) {
		printf("\033[0;36mCAN-SL Receiver: ERROR allocating memory\033[0m\n");
		fflush(0);
	}

	//Here maybe should be checked if the ID of the Sender is correct
	//(we are expecting sender with this ID)
	usleep(10000);
	if (id == 0x000aaa) {
		return 0;
	}
	printf("\033[0;36mCAN-SL Receiver:\033[0;31m Rejected the frame\033[0m\n");
	return -1;
//	printf("\033[0;36mCAN-SL Receiver: Memory allocated: %ld\033[0m\n", (long)(*data));
//	fflush(0);
}

void cantp_sndr_tx_done_cb(void)
{
	printf("CAN-SL Sender: TX Done \n"); fflush(0);
	msync(rcvr_end_sem, sizeof(size_t), MS_SYNC);
	sem_post(rcvr_end_sem);
}

void receiver_task(cantp_params_t *rcvr_params)
{
	printf("\033[0;33mInitializing the Receiver side\033[0m ");fflush(0);
	static cantp_rxtx_status_t ctp_rcvr_state = { 0 };

	long cdrv_rcvr_tx_delay = 1000;

	fake_can_init(cdrv_rcvr_tx_delay, "\033[0;34mCAN-LL Receiver\033[0m",
										&ctp_rcvr_state, CAN_LL_RECEIVER);

	printf("\033[0;33mReceiver pid = %d \033[0m\n", getpid());

	msync(rcvr_params, sizeof(cantp_params_t), MS_SYNC);
	printf("cantp_rcvr_params_init:\n"
			"block_size = %d\n"
			"st_min_us = %d\n"
			"st_min = %x\n"
			"wft_num = %d\n"
			"wft_max = %d\n"
			"wft_tim_us = %d\n",
			rcvr_params->block_size,
			rcvr_params->st_min_us,
			rcvr_params->st_min,
			rcvr_params->wft_num,
			rcvr_params->wft_max,
			rcvr_params->wft_tim_us);fflush(0);
	if (cantp_rcvr_params_init(&ctp_rcvr_state, rcvr_params, "Receiver") < 0) {
		return;
	}

	static cbtimer_t ctp_rcvr_timer;
	cantp_set_rcvr_timer_ptr(&ctp_rcvr_timer, &ctp_rcvr_state);
	cbtimer_set_cb(&ctp_rcvr_timer, cantp_rcvr_t_cb, &ctp_rcvr_state);
	
//	static cbtimer_t ctp_st_timer;
//	cantp_set_st_timer_ptr(&ctp_st_timer, &ctp_rcvr_state);
//	cbtimer_set_cb(&ctp_st_timer, cantp_tx_st_t_cb, &ctp_rcvr_state);

	msync(rcvr_start_sem, sizeof(size_t), MS_SYNC);
	sem_post(rcvr_start_sem);

	do {
		usleep(1000);
		fake_can_rx_task(&ctp_rcvr_state);
	} while (1);
}

static inline int args_get(int argc, char **argv, uint16_t *tx_len,
							uint32_t *canll_delay, cantp_params_t *params)
{
	int argnum = 1;
	if (argc <= 2) {
		printf("usage example: cantp_test -tx_len 30 -block_size 4 -st_min 1000 "
				"-canll_delay 10000 \n");
		return -1;
	}
	while (argnum < argc) {
		if ( argv[argnum][0] == '-' ) {
			if (argv[argnum+1] == NULL) {
				printf("missing argument for option '-%c'\n\r", argv[argnum][1]);
				return -1;
			}
			if (strcmp("canll_delay", (argv[argnum]+1)) == 0) {
				*canll_delay = atoi((char *)(argv[argnum+1]));
				printf("CANLLdelay=%d\n", *canll_delay);
			} else if (strcmp("st_min", (argv[argnum]+1)) == 0) {
				params->st_min_us = atoi((char *)(argv[argnum+1]));
				printf("STmin=%d\n", params->st_min_us);
			} else if (strcmp("block_size", (argv[argnum]+1)) == 0) {
				params->block_size = atoi((char *)(argv[argnum+1]));
				printf("block_size=%d\n", params->block_size);
			} else if (strcmp("tx_len", (argv[argnum]+1)) == 0) {
				*tx_len = atoi((char *)(argv[argnum+1]));
				printf("tx_len=%d\n", *tx_len);
			} else if (strcmp("wft_tim_us", (argv[argnum]+1)) == 0) {
				params->wft_tim_us = atoi((char *)(argv[argnum+1]));
				printf("wft_tim_us=%d\n", params->wft_tim_us);
			} else if (strcmp("wft_max", (argv[argnum]+1)) == 0) {
				params->wft_max = atoi((char *)(argv[argnum+1]));
				printf("wft_max=%d\n", params->wft_max);
			}
		} else {
			printf("\r\nError: wrong parameter '%s'\n", argv[argnum]);
		}
		argnum+=2;
	} // while
	return 0;
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
	uint32_t canll_delay = 100000;
	uint16_t tx_len = 24;

	//here are variables defined in shared memory
	cantp_params_t *rcvr_params;
	rcvr_params = (cantp_params_t *) mmap(NULL, sizeof(cantp_params_t),
			PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
	if (rcvr_params == MAP_FAILED) {
		printf("Couldn't initialize st_min mapping\n");
		return EXIT_FAILURE;
	}

	rcvr_params->id = 0xbbb;
	rcvr_params->idt = 0;

	if (args_get(argc, argv, &tx_len, &canll_delay, rcvr_params) < 0) {
		return EXIT_FAILURE;
	}

	msync(rcvr_params, sizeof(cantp_params_t), MS_SYNC);

	rcvr_start_sem = (sem_t*) mmap(NULL, sizeof(sem_t),
			PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
	if (rcvr_start_sem == MAP_FAILED) {
		printf("Couldn't initialize semaphore mapping\n");
		return EXIT_FAILURE;
	}
	sem_init(rcvr_start_sem, 1, 0);

	rcvr_end_sem = (sem_t*) mmap(NULL, sizeof(sem_t),
				PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
	if (rcvr_end_sem == MAP_FAILED) {
		printf("Couldn't initialize semaphore mapping\n");
		return EXIT_FAILURE;
	}
	sem_init(rcvr_end_sem, 1, 0);

	printf("\033[0;35mInitializing the Sender side\033[0m ");
	static cantp_rxtx_status_t cantp_sndr_state;
	fake_can_init(canll_delay, "\033[1;30mCAN-LL Sender\033[0m",
											&cantp_sndr_state, CAN_LL_SENDER);

	cantp_params_t sndr_params;
	sndr_params.st_min_us = 127000;
	sndr_params.block_size = 0;
	if (cantp_rcvr_params_init(&cantp_sndr_state, &sndr_params, "Sender") < 0) {
		return EXIT_FAILURE;
	}

	//Maybe is better to use threads instead of fork()
	//but someone can improve it if he wants
	pid_t pid = fork();
	if (pid == (pid_t) 0) {
		//This is the child process.
		msync(rcvr_params, sizeof(cantp_params_t), MS_SYNC);
		receiver_task(rcvr_params);
		printf("Receiver END\n"); fflush(0);
		msync(rcvr_end_sem, sizeof(size_t), MS_SYNC);
		sem_post(rcvr_end_sem);
		return EXIT_SUCCESS;
	} else if (pid < (pid_t) 0) {
		fprintf(stderr, "Fork failed.\n");
		return EXIT_FAILURE;
	}
	printf("\033[0;35mSender pid = %d\033[0m\n", getpid());
//	printf("pid = %d\n", pid);

	uint8_t *data = malloc(tx_len);
	for (uint16_t i = 0; i < tx_len; i++) {
		data[i] = (uint8_t)(0xff & i) + 1;
	}

	static cbtimer_t ctp_sndr_timer;
	cantp_set_sndr_timer_ptr(&ctp_sndr_timer, &cantp_sndr_state);
	cbtimer_set_cb(&ctp_sndr_timer, cantp_sndr_t_cb, &cantp_sndr_state);

	static cbtimer_t ctp_rcvr_timer;
	cantp_set_rcvr_timer_ptr(&ctp_rcvr_timer, &cantp_sndr_state);
	cbtimer_set_cb(&ctp_rcvr_timer, cantp_rcvr_t_cb, &cantp_sndr_state);

	static cbtimer_t ctp_st_timer;
	cantp_set_st_timer_ptr(&ctp_st_timer, &cantp_sndr_state);
	cbtimer_set_cb(&ctp_st_timer, NULL, &cantp_sndr_state);

	//Wait some time for Receiver to be initialized
	usleep(10000);
	//First check if the receiver has ended initialization on error
	int rcvr_end_sem_val;
	sem_getvalue(rcvr_end_sem, &rcvr_end_sem_val);
	if (rcvr_end_sem_val) {
		return EXIT_FAILURE;
	}
	//Wait until Receiver finishes initialization
	sem_wait(rcvr_start_sem);

	printf("\033[0;35mSender can send now \033[0m\n");
	cantp_send(&cantp_sndr_state, 0xaaa, 0, data, tx_len);

	int semval;
	do {
		fake_can_rx_task(&cantp_sndr_state);
		sem_getvalue(rcvr_end_sem, &semval);
	} while (semval == 0);

	sem_wait(rcvr_end_sem);
	//Stop Receiver's receiving process
	kill(pid, SIGHUP);
	printf("EndMain\n");fflush(0);
	return 0;
}
