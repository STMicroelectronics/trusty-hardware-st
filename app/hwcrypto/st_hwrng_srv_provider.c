/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define TLOG_TAG "hwrng_ST_srv"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uapi/err.h>

#include <hwcrypto/hwrng_dev.h>
#include <trusty/time.h>
#include <trusty_log.h>
#include <lib/hwpsa/hwpsa.h>

#pragma message "Compiling STM HWRNG provider"

int hwrng_dev_init(void) {
    TLOGE("Init STM HWRNG service provider\n");
    return NO_ERROR;
}

int trusty_rng_hw_rand(uint8_t* buf, size_t buf_len) {
	hwpsa_session_t session;
	psa_status_t status = PSA_SUCCESS;
	int ret = NO_ERROR;

	session = hwpsa_open();

	ret = psa_generate_random_ss(session, buf, buf_len);

	if (status != PSA_SUCCESS) {
		ret = ERR_GENERIC;
	}

	hwpsa_close(session);

	return ret;
 }

