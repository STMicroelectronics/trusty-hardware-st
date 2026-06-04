// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2022-2024, Arm Limited and Contributors. All rights reserved.
 * Copyright (c) 2025 STMicroelectronics  All rights reserved.
 *
 */

#include <psa/client.h>
#include <rse_comms.h>
#include "rse_comms_protocol.h"
#include <stdint.h>
#include <string.h>
#include <lk/trace.h>

#define TLOG_TAG "rse_comms"
#include <trusty_log.h>

/*
 * Union as message space and reply space are never used at the same time,
 * and this saves space as we can overlap them.
 */
 /* TF_M M33 : union __packed __aligned(4) rse_comms_io_buffer_t {
    ==> */
union __attribute__((aligned(4))) rse_comms_io_buffer_t {
	struct serialized_rse_comms_msg_t msg;
	struct serialized_rse_comms_reply_t reply;
} __PACKED;

static rse_get_max_message_size_callback_t rse_mbx_size_cb;
static rse_send_callback_t rse_mbx_send_cb;
static rse_recv_callback_t rse_mbx_recv_cb;
static uint16_t rse_client_id = ~0;

static inline int rse_mbx_get_max_message_size(size_t *size)
{
	return rse_mbx_size_cb ? rse_mbx_size_cb(size) : -1;
}

static inline int rse_mbx_send_data(const uint8_t *send_buffer, size_t size)
{
	return rse_mbx_send_cb ? rse_mbx_send_cb(send_buffer, size) : -1;
}

static inline int rse_mbx_receive_data(uint8_t *receive_buffer, size_t *size)
{
	return rse_mbx_recv_cb ? rse_mbx_recv_cb(receive_buffer, size) : -1;
}

static uint8_t select_protocol_version2(size_t in_size_total, size_t out_size_total)
{
	size_t comms_mbx_msg_size;
	size_t comms_embed_msg_min_size;
	size_t comms_embed_reply_min_size;

	if (rse_mbx_get_max_message_size(&comms_mbx_msg_size))
	/* TODO : TF_M-M33 */
	{}
		/*panic();*/

	comms_embed_msg_min_size = sizeof(struct serialized_rse_comms_header_t)
		+ sizeof(struct rse_embed_msg_t)
		- PLAT_RSE_COMMS_PAYLOAD_MAX_SIZE;

	comms_embed_reply_min_size =
		sizeof(struct serialized_rse_comms_header_t)
		+ sizeof(struct rse_embed_reply_t)
		- PLAT_RSE_COMMS_PAYLOAD_MAX_SIZE;

	/* Use embed if we can pack into one message and reply, else use
	 * pointer_access. The underlying mailbox transport protocol uses a
	 * single uint32_t to track the length, so the amount of data that
	 * can be in a message is 4 bytes less than rse_mbx_get_max_message_size
	 * reports.
	 *
	 * TODO tune this with real performance numbers, it's possible a
	 * pointer_access message is less performant than multiple embed
	 * messages due to ATU configuration costs to allow access to the
	 * pointers.
	 */
	if ((comms_embed_msg_min_size + in_size_total >
	     comms_mbx_msg_size - sizeof(uint32_t)) ||
	    (comms_embed_reply_min_size + out_size_total >
	     comms_mbx_msg_size - sizeof(uint32_t)))
		return RSE_COMMS_PROTOCOL_POINTER_ACCESS;
	else
		return RSE_COMMS_PROTOCOL_EMBED;
}


void rse_set_client_id(uint16_t client_id)
{
	rse_client_id = client_id;
}

int rse_register_cb(rse_send_callback_t send_cb, rse_recv_callback_t rcv_cb,
		    rse_get_max_message_size_callback_t size_cb)
{
	rse_mbx_recv_cb = rcv_cb;
	rse_mbx_send_cb = send_cb;
	rse_mbx_size_cb = size_cb;

	return 0;
}


psa_status_t psa_call(psa_handle_t handle, int32_t type,
					  psa_status_t *return_val,
					  uint8_t *data_in, size_t data_in_len,
		              uint8_t *data_out, size_t data_out_len)
{
	static union rse_comms_io_buffer_t io_buf;
	static uint8_t seq_num = 1U;
	psa_status_t status;
	size_t msg_size;
	size_t reply_size = sizeof(io_buf.reply);
	int err;

	io_buf.msg.header.seq_num = seq_num;

	/*
	 * No need to distinguish callers (currently concurrent calls
	 * are not supported).
	 */
	io_buf.msg.header.client_id =  rse_client_id;

	io_buf.msg.header.protocol_ver =
		select_protocol_version2(data_in_len, data_out_len);

	status = rse_protocol_serialize_msg(handle, type,
											data_in,
											data_out,
											&io_buf.msg,
											&msg_size);

	if (status != PSA_SUCCESS)
		return status;

	err = rse_mbx_send_data((uint8_t *)&io_buf.msg, msg_size);
	if (err != 0)
		return PSA_ERROR_COMMUNICATION_FAILURE;

	/*
	 * Poisoning the message buffer (with a known pattern).
	 * Helps in detecting hypothetical RSE communication bugs.
	 */
	memset(&io_buf.msg, 0xA5, msg_size);

	err = rse_mbx_receive_data((uint8_t *)&io_buf.reply, &reply_size);
	if (err != 0)
		return PSA_ERROR_COMMUNICATION_FAILURE;

	status = rse_protocol_deserialize_reply(data_in, data_out, return_val,
						&io_buf.reply, reply_size);
	if (status != PSA_SUCCESS)
		return status;

	/* Clear the mailbox message buffer to remove assets from memory */
	memset(&io_buf, 0x0, sizeof(io_buf));

	seq_num++;
	return PSA_SUCCESS;
}
