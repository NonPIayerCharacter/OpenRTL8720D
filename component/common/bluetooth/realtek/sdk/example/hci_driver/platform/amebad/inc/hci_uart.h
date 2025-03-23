/*
 *******************************************************************************
 * Copyright(c) 2022, Realtek Semiconductor Corporation. All rights reserved.
 *******************************************************************************
 */

#ifndef _HCI_UART_H_
#define _HCI_UART_H_

#include "hci/hci_common.h"

enum {
    UART_RX,
    UART_TX,
    UART_TRX,
};

typedef struct
{
    /* UART Open */
    uint8_t (*open)(void);

    /* UART Close */
    uint8_t (*close)(void);

    /* UART Free */
    uint8_t (*free_ops)(void);

    /* UART Send */
    uint16_t (*send)(uint8_t *buf, uint16_t len);

    /* UART Read */
    uint16_t (*read)(uint8_t *buf, uint16_t len);

    /* UART Set Rx ind */
    uint8_t (*set_rx_ind)(HCI_RECV_IND rx_ind);

    /* UART Set Tx Run */
    uint8_t (*set_tx_run)(uint8_t tx_run);

    /* UART Set BaudRate */
    uint8_t (*set_bdrate)(uint32_t baudrate);

    /* UART Set IRQ */
    void (*set_irq)(uint8_t trx_flag, uint8_t en);

    /* UART Bridge Open */
    void (*bridge_open)(bool flag);

    /* UART Bridge to HCI */
    void (*bridge_to_hci)(uint8_t rc);
} HCI_UART_OPS;

extern HCI_UART_OPS hci_uart_ops;

static inline uint8_t hci_uart_set_bdrate(uint32_t baudrate) 
{
    return hci_uart_ops.set_bdrate(baudrate);
}

static inline uint8_t hci_uart_set_rx_ind(HCI_RECV_IND rx_ind) 
{
    return hci_uart_ops.set_rx_ind(rx_ind);
}

static inline uint8_t hci_uart_set_tx_run(uint8_t tx_run) 
{
    return hci_uart_ops.set_tx_run(tx_run);
}

static inline uint16_t hci_uart_send(uint8_t *buf, uint16_t len)
{
    return hci_uart_ops.send(buf, len);
}

static inline uint16_t hci_uart_read(uint8_t *buf, uint16_t len)
{
    return hci_uart_ops.read(buf, len);
}

static inline uint8_t hci_uart_open(void)
{
    return hci_uart_ops.open();
}

static inline uint8_t hci_uart_close(void)
{
    return hci_uart_ops.close();
}

static inline uint8_t hci_uart_free(void)
{
    return hci_uart_ops.free_ops();
}

static inline void hci_uart_set_irq(uint8_t trx_flag, uint8_t en)
{
    return hci_uart_ops.set_irq(trx_flag, en);
}

static inline void hci_uart_bridge_open(bool flag)
{
    return hci_uart_ops.bridge_open(flag);
}

static inline void hci_uart_bridge_to_hci(uint8_t rc)
{
    return hci_uart_ops.bridge_to_hci(rc);
}

#endif
