/*
 * Copyright (c) 2022-2023, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

/* ANDROID M33 TF-M REF : device/stm/stm32mp2-system/tf-m-stm32mp2/platform/ext/target/stm/common/stm32mp2xx/secure/tfm_builtin_key_ids.h */

#ifndef __TFM_BUILTIN_KEY_IDS_H__
#define __TFM_BUILTIN_KEY_IDS_H__

/**
 * \brief The persistent key identifiers for TF-M builtin keys.
 *
 * \note The value of TFM_BUILTIN_KEY_ID_MIN (and therefore of the whole range) is
 *       completely arbitrary except for being inside the PSA builtin keys range.
 *       The range is specified by the limits defined through MBEDTLS_PSA_KEY_ID_BUILTIN_MIN
 *       and MBEDTLS_PSA_KEY_ID_BUILTIN_MAX
 */
enum tfm_builtin_key_id_t {
	TFM_BUILTIN_KEY_ID_MIN = 0x7FFF815Bu,
	TFM_BUILTIN_KEY_ID_HUK,
	TFM_BUILTIN_KEY_ID_IAK,
#ifdef TFM_PARTITION_DELEGATED_ATTESTATION
	TFM_BUILTIN_KEY_ID_DAK_SEED,
#endif /* TFM_PARTITION_DELEGATED_ATTESTATION */
	TFM_BUILTIN_KEY_ID_PLAT_SPECIFIC_MIN = 0x7FFF816Bu,
	TFM_BUILTIN_KEY_ID_TRUSTY_DK,
	TFM_BUILTIN_KEY_ID_TRUSTY_RPMB,
/* TO DO add TFM_BUILTIN_KEY_ID_TRUSTY_SK in TF_M M33 */
	TFM_BUILTIN_KEY_ID_TRUSTY_SK,
	TFM_BUILTIN_KEY_ID_MAX = 0x7FFF817Bu,
};

#endif /* __TFM_BUILTIN_KEY_IDS_H__ */
