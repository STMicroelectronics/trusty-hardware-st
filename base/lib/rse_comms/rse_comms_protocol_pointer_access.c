// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2022-2024, Arm Limited. All rights reserved.
 *
 */
#include <assert.h>

#include "rse_comms_protocol_common.h"
#include "rse_comms_protocol_pointer_access.h"

#define TLOG_TAG "rse_comms"
#include <trusty_log.h>

psa_status_t rse_protocol_pointer_access_serialize_msg(psa_handle_t handle,
								int16_t type,
								const uint8_t *in_data,
								const uint8_t *out_data,
								struct rse_pointer_access_msg_t *msg,
								size_t *msg_len)
{
	unsigned int i;
	int16_t payload_size = 0;
	struct psa_iov_len* head_data_in;

	assert(msg);
	assert(msg_len);
	assert(in_data);

	head_data_in = (struct psa_iov_len*) in_data;

	msg->ctrl_param = PARAM_PACK(type, head_data_in->iov_in_len, head_data_in->iov_out_len);
	msg->handle = handle;

	/* Fill msg iovec lengths */
	for (i = 0U; i < head_data_in->iov_in_len; ++i) {
		msg->io_sizes[i] = head_data_in->io_in_size[i];
	    msg->host_ptrs[i] = (uint64_t)(in_data + sizeof(struct psa_iov_len) + payload_size);
		payload_size = payload_size + head_data_in->io_in_size[i];
	}
	payload_size = 0;
	for (i = 0U; i < (head_data_in->iov_out_len -1); ++i) {
		msg->io_sizes[head_data_in->iov_in_len + i] = head_data_in->io_out_size[i+1];
        msg->host_ptrs[head_data_in->iov_in_len + i] = (uint64_t)(out_data + payload_size);
	    payload_size = payload_size + head_data_in->io_out_size[i];
	}

	*msg_len = sizeof(*msg);

	return PSA_SUCCESS;
}


psa_status_t rse_protocol_pointer_access_deserialize_reply(const uint8_t *in_data,
							   psa_status_t
							   *return_val,
							   const struct
							   rse_pointer_access_reply_t
							   *reply,
							   size_t reply_size)
{
    struct psa_iov_len* head_data_in;

	assert(reply);
	assert(return_val);

	head_data_in = (struct psa_iov_len*) in_data;

	/*  compute the len that can be filled */
	reply_size /= 4;
	if (reply_size > (head_data_in->iov_out_len-1))
		return PSA_ERROR_INVALID_ARGUMENT;

	*return_val = reply->return_val;

	return PSA_SUCCESS;
}
