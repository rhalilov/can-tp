/*
 * cantp_glue_lin.h
 *
 *  Created on: May 21, 2022
 *      Author: refo
 */

#ifndef _CANTP_GLUE_LIN_H_
#define _CANTP_GLUE_LIN_H_

void cantp_result_cb(int result);
void cantp_init(cantp_context_t *cantp_ctx);
void cantp_wait_for_result(void);

#endif /* _CANTP_GLUE_LIN_H_ */
