/*
 *******************************************************************************
 * Copyright(c) 2022, Realtek Semiconductor Corporation. All rights reserved.
 *******************************************************************************
 */

#ifndef _HCI_DBG_H_
#define _HCI_DBG_H_

#include "ameba_soc.h"
#include "hci/hci_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EFUSE_SW_USE_FLASH_PATCH     BIT0
#define EFUSE_SW_BT_FW_LOG           BIT1
#define EFUSE_SW_RSVD                BIT2
#define EFUSE_SW_IQK_HCI_OUT         BIT3
#define EFUSE_SW_UPPERSTACK_SWITCH   BIT4
#define EFUSE_SW_TRACE_SWITCH        BIT5
#define EFUSE_SW_DRIVER_DEBUG_LOG    BIT6
#define EFUSE_SW_RESERVE             BIT7

extern uint32_t hci_sw_val;
#define CHECK_SW(x)                  (hci_sw_val & x)

#define hci_board_debug              DBG_8195A

#define HCI_ASSERT(...)    \
    do                     \
    {                      \
    } while (0)

#define HCI_PRINT(fmt, ...)                  \
    do                                       \
    {                                        \
        hci_board_debug("\n\r"fmt, ##__VA_ARGS__); \
    } while (0)

#define HCI_ERR(fmt, ...)                                                                 \
    do                                                                                    \
    {                                                                                     \
        hci_board_debug("\n\r%s:%d(err) " fmt "\n\r", __FUNCTION__, __LINE__, ##__VA_ARGS__);     \
    } while (0)

#define HCI_DBG(fmt, ...)                                                         \
    do                                                                                    \
    {                                                                                     \
        if (!CHECK_SW(EFUSE_SW_DRIVER_DEBUG_LOG))                                                            \
            hci_board_debug("\n\r%s:%d(dbg) " fmt "\n\r", __FUNCTION__, __LINE__, ##__VA_ARGS__); \
    } while (0)

#define HCI_INFO(fmt, ...)                                                        \
    do                                                                                    \
    {                                                                                     \
        if (!CHECK_SW(EFUSE_SW_DRIVER_DEBUG_LOG))                                                            \
            hci_board_debug("\n\r%s:%d(info) " fmt "\n\r", __FUNCTION__, __LINE__, ##__VA_ARGS__);\
    } while (0)

#define HCI_WARN(fmt, ...)                                                        \
    do                                                                                    \
    {                                                                                     \
        if (!CHECK_SW(EFUSE_SW_DRIVER_DEBUG_LOG))                                                            \
            hci_board_debug("\n\r%s:%d(warn) " fmt "\n\r", __FUNCTION__, __LINE__, ##__VA_ARGS__);\
    } while (0)

#define HCI_DUMP(hdr, hdr_len, data, data_len) \
    do                                         \
    {                                          \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif