/*
 * fake_can_linux.h
 *
 *  Created on: May 21, 2022
 *      Author: refo
 */

#ifndef _FAKE_CAN_LINUX_H_
#define _FAKE_CAN_LINUX_H_

int fake_can_tx(uint32_t id, uint8_t idt, uint8_t dlc, uint8_t *data);
int candrv_rx_task(void);
int fake_can_init(void *params);
void fake_cantx_confirm_cb(void *params);

#endif /* _FAKE_CAN_LINUX_H_ */
