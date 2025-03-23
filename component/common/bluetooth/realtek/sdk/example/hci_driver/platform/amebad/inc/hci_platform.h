/*
 *******************************************************************************
 * Copyright(c) 2022, Realtek Semiconductor Corporation. All rights reserved.
 *******************************************************************************
 */

#ifndef _HCI_PLATFORM_H_
#define _HCI_PLATFORM_H_

#include "hci/hci_common.h"
#include "./hci_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_HCI_RESET

#define hci_platform_START_IQK
#define hci_platform_WRITE_IQK

#define HCI_START_IQK_TIMES            4
#define HCI_READ_THER_TIMES            4

#define HCI_IQK_DATA_LEN               4
#define HCI_DEFAULT_LMP_SUBVER         0x8721
#define HCI_WRITE_IQK_DATA_LEN         0x13

typedef struct {
    uint8_t  offset;
    uint16_t value;
} HCI_IQK_DATA;

extern HCI_IQK_DATA hci_iqk_data[HCI_START_IQK_TIMES];

uint8_t hci_platform_check_iqk(void);
uint8_t hci_platform_start_iqk(void);
uint8_t hci_platform_get_iqk_data(uint8_t *data, uint8_t len);

void hci_platform_record_chipid(uint8_t chipid);

void hci_platform_get_baudrate(uint8_t* baudrate, uint8_t len);
void hci_platform_cfg_bd_addr(uint8_t* bdaddr);
uint8_t hci_platform_set_baudrate(void);

void hci_platform_dl_patch_done(void);
uint8_t hci_platform_dl_patch_init(void);
uint8_t hci_platform_get_patch_cmd_len(uint8_t *cmd_len);
uint8_t hci_platform_get_patch_cmd_buf(uint8_t *cmd_buf, uint8_t cmd_len);

uint8_t hci_platform_init(void);
uint8_t hci_platform_deinit(void);
uint8_t hci_platform_init_done(void);

#ifdef __cplusplus
}
#endif

#endif