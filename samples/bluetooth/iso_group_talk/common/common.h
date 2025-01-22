/* Copyright (c) 2025 Tailored Technology Ltd
 * Copyright (c) 2021-2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SAMPLES_BLUETOOTH_ISO_GROUP_TALK_COMMON_COMMON_H_
#define ZEPHYR_SAMPLES_BLUETOOTH_ISO_GROUP_TALK_COMMON_COMMON_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>

#ifdef __cplusplus
extern "C" {
#endif

/* If the size of the payload struct is increased, then increase the value
   of the following configs by **at least** the value it has increased by
   CONFIG_BT_ISO_TX_MTU  (in file proj.conf)
   CONFIG_BT_CTLR_ISO_TX_BUFFER_SIZE (in file overlay-bt_ll_sw_split.conf)
   CONFIG_BT_CTLR_ADV_ISO_PDU_LEN_MAX (in file overlay-bt_ll_sw_split.conf)
   */
struct app_bis_payload {
	uint32_t    send_count;
	uint8_t 	bis_index;  /* 1..N */
	uint8_t 	padding[3];
};






#if 0
/** Initialisation Vector size in bytes */
#define BT_EAD_IV_SIZE	       8
/** MIC size in bytes */
#define BT_EAD_MIC_SIZE	       4

/** Get the size (in bytes) of the encrypted advertising data for a given
 *  payload size in bytes.
 */
#define BT_EAD_ENCRYPTED_PAYLOAD_SIZE(payload_size)                                                \
	((payload_size) + BT_EAD_RANDOMIZER_SIZE + BT_EAD_MIC_SIZE)

/**
 * @brief Decrypt and authenticate the given encrypted advertising data.
 *
 * @note The term `advertising structure` is used to describe the advertising
 *       data with the advertising type and the length of those two.
 *
 * @param[in]  session_key Key of 16 bytes used for the encryption.
 * @param[in]  iv Initialisation Vector used to generate the `nonce`.
 * @param[in]  encrypted_payload Encrypted Advertising Data received. This
 *             should only contain the advertising data from the received
 *             advertising structure, not the length nor the type.
 * @param[in]  encrypted_payload_size Size of the received advertising data in
 *             bytes. Should be equal to the length field of the received
 *             advertising structure, minus the size of the type (1 byte).
 * @param[out] payload Decrypted advertising payload. Use @ref
 *             BT_EAD_DECRYPTED_PAYLOAD_SIZE to get the right size.
 *
 * @retval 0 Data have been correctly decrypted and authenticated.
 * @retval -EIO Error occurred during the decryption or the authentication.
 * @retval -EINVAL One of the argument is a NULL pointer or @p
 *                 encrypted_payload_size is less than @ref
 *                 BT_EAD_RANDOMIZER_SIZE + @ref BT_EAD_MIC_SIZE.
 */
int bt_ead_decrypt(const uint8_t session_key[BT_EAD_KEY_SIZE], const uint8_t iv[BT_EAD_IV_SIZE],
		   const uint8_t *encrypted_payload, size_t encrypted_payload_size,
		   uint8_t *payload);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SAMPLES_BLUETOOTH_ISO_GROUP_TALK_COMMON_COMMON_H_ */
