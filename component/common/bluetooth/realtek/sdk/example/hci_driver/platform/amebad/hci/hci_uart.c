/*
 *******************************************************************************
 * Copyright(c) 2022, Realtek Semiconductor Corporation. All rights reserved.
 *******************************************************************************
 */

#include <string.h>
#include "osif.h"
#include "ameba_soc.h"
#include "../inc/hci_uart.h"
#include "hci_dbg.h"

#define HCI_UART_IDX  1      /* only [0, 1, 3] */

#if (HCI_UART_IDX == 0)
    #define HCI_UART_OUT
    #define HCI_UART_DEV    UART0_DEV
    #define HCI_UART_IRQ    UART0_IRQ
    #if 1
        #define HCI_TX_PIN  _PA_18
        #define HCI_RX_PIN  _PA_19
        #define HCI_CTS_PIN _PA_17
        #define HCI_RTS_PIN _PA_16    /* BT_LOG */
    #else
        #define HCI_TX_PIN  _PA_21
        #define HCI_RX_PIN  _PA_22
        #define HCI_CTS_PIN _PA_24
        #define HCI_RTS_PIN _PA_23
    #endif
#elif (HCI_UART_IDX == 3)
    #define HCI_UART_OUT
    #define HCI_UART_DEV    UART3_DEV
    #define HCI_UART_IRQ    UARTLP_IRQ
    #define HCI_TX_PIN      _PA_26
    #define HCI_RX_PIN      _PA_25
    #define HCI_CTS_PIN     _PA_25
    #define HCI_RTS_PIN     _PA_27
#else
    /* CTS PA0 --- RTS_PIN
     * TX  PA2 --- RX_PIN
     * RX  PA4 --- TX_PIN
     */
    #define HCI_UART_DEV    UART1_DEV
    #define HCI_UART_IRQ    UART1_IRQ
#endif

// #define HCI_UART_TX_PIN          (PIN_UART3_TX)
// #define HCI_UART_RX_PIN          (PIN_UART3_RX)
#define HCI_UART_TX_FIFO_SIZE    (16)
#define HCI_UART_RX_FIFO_SIZE    (16)
#define HCI_UART_RX_BUF_SIZE     (0x2000)   /* RX buffer size 8K */
#define HCI_UART_IRQ_PRIO        (10)
#define HCI_UART_RX_ENABLE_SIZE  (512)      /* Only 512 left to read */
#define HCI_UART_RX_DISABLE_SIZE (128)      /* Only 128 left to write */

static struct amebad_uart_t
{
    /* UART Structure */
    UART_InitTypeDef init_struct;
    uint32_t ier;

    /* UART RX Indication */
    HCI_RECV_IND rx_ind;

    /* UART RX RingBuf */
    uint8_t *ring_buffer;
    uint32_t ring_buffer_size;
    uint32_t write_ptr;
    uint32_t read_ptr;
    uint8_t rx_disabled;

    /* UART TX */
    uint8_t tx_run;
    uint8_t *tx_buf;
    uint8_t *tx_len;
    void    *tx_done_sem;

    /* UART Bridge */
    uint8_t bridge_flag;
} *amebad_uart = NULL;

_WEAK void bt_uart_bridge_putc(uint8_t tx_data)
{
    (void)tx_data;
}

void amebad_uart_bridge_open(uint8_t flag)
{
    if (amebad_uart)
        amebad_uart->bridge_flag = flag;
    else
        HCI_ERR("amebad_uart is NULL!");
}

void amebad_uart_bridge_to_hci(uint8_t rc)
{
    UART_CharPut(HCI_UART_DEV, rc);
}

void amebad_uart_hci_to_bridge(uint8_t rc)
{
    bt_uart_bridge_putc(rc);
}

static uint8_t amebad_uart_set_bdrate(uint32_t baudrate)
{
    UART_SetBaud(HCI_UART_DEV, baudrate);
    HCI_INFO("Set baudrate to %d success!", baudrate);
    return HCI_SUCCESS;
}

static uint8_t amebad_uart_set_rx_ind(HCI_RECV_IND rx_ind)
{
    amebad_uart->rx_ind = rx_ind;
    return HCI_SUCCESS;
}

static uint8_t amebad_uart_set_tx_run(uint8_t tx_run)
{
    amebad_uart->tx_run = tx_run;
    return HCI_SUCCESS;
}

static inline uint16_t amebad_uart_rx_to_read_space(void)
{
    return (amebad_uart->write_ptr + amebad_uart->ring_buffer_size - amebad_uart->read_ptr) % amebad_uart->ring_buffer_size;
}

static inline uint16_t amebad_uart_rx_to_write_space(void)
{
    return (amebad_uart->read_ptr + amebad_uart->ring_buffer_size - amebad_uart->write_ptr - 1) % amebad_uart->ring_buffer_size;
}

static inline void transmit_chars(void)
{
    int count;
    if (!amebad_uart)
    {
        HCI_ERR("amebad_uart is NULL\r\n");
        return;
    }

    uint16_t max_count = HCI_UART_TX_FIFO_SIZE;

    while (amebad_uart->tx_len > 0 && max_count-- > 0) {
        UART_CharPut(HCI_UART_DEV, *(amebad_uart->tx_buf));
        amebad_uart->tx_buf++;
        amebad_uart->tx_len--;
    }

    if (amebad_uart->tx_len == 0) {
        if (amebad_uart->ier & RUART_IER_ETBEI)
        {
            amebad_uart->ier &= ~RUART_IER_ETBEI;
            UART_INTConfig(HCI_UART_DEV, RUART_IER_ETBEI, DISABLE);
        }
        if (amebad_uart->tx_done_sem)
            osif_sem_give(amebad_uart->tx_done_sem);
    }
}

extern uint8_t flag_for_hci_trx;
static inline void receive_chars(void)
{
    uint8_t ch;
    uint16_t write_len = amebad_uart_rx_to_write_space();
    uint16_t max_count = (write_len > HCI_UART_RX_FIFO_SIZE) ? HCI_UART_RX_FIFO_SIZE : write_len;

    if (!amebad_uart)
    {
        UART_CharGet(HCI_UART_DEV, &ch);
        HCI_ERR("amebad_uart is NULL, data:%x", ch);
        return;
    }

    if (amebad_uart->bridge_flag) {
        while (UART_Readable(HCI_UART_DEV) && max_count-- > 0) {
            UART_CharGet(HCI_UART_DEV, &ch);
            amebad_uart_hci_to_bridge(ch);
        }
    } else {
        while (UART_Readable(HCI_UART_DEV) && max_count-- > 0) {
            UART_CharGet(HCI_UART_DEV, &ch);
            amebad_uart->ring_buffer[amebad_uart->write_ptr++] = ch;
            amebad_uart->write_ptr %= amebad_uart->ring_buffer_size;
        }

        if (!amebad_uart->rx_disabled && amebad_uart_rx_to_write_space() < HCI_UART_RX_DISABLE_SIZE) {
            /* We disable received data available and rx timeout interrupt, then
             * the rx data will stay in UART FIFO, and RTS will be pulled high if
             * the watermark is higher than rx trigger level.
             */
            UART_INTConfig(HCI_UART_DEV, RUART_IER_ERBI | RUART_IER_ETOI, DISABLE);
            amebad_uart->rx_disabled = 1;
            HCI_INFO("amebad_uart rx disable!");
        }

        if (0 == flag_for_hci_trx) {
            if (amebad_uart->rx_ind)
                amebad_uart->rx_ind();
        }
    }
}

static uint32_t amebad_uart_irq(void *data)
{
    (void)data;
    volatile u8 reg_iir;
    u8 int_id;
    u32 reg_val;

    reg_iir = UART_IntStatus(HCI_UART_DEV);
    /* No pending IRQ */
    if ((reg_iir & RUART_IIR_INT_PEND) != 0)
        return 0;

    int_id = (reg_iir & RUART_IIR_INT_ID) >> 1;
    switch (int_id)
    {
    case RUART_LP_RX_MONITOR_DONE:
        reg_val = UART_RxMonitorSatusGet(HCI_UART_DEV);
        HCI_DBG("monitor done");
        break;
    case RUART_MODEM_STATUS:
        reg_val = UART_ModemStatusGet(HCI_UART_DEV);
        break;
    case RUART_TX_FIFO_EMPTY:
        transmit_chars();
        break;
    case RUART_RECEIVER_DATA_AVAILABLE:
    case RUART_TIME_OUT_INDICATION:
        receive_chars();
        break;
    case RUART_RECEIVE_LINE_STATUS:
        reg_val = (UART_LineStatusGet(HCI_UART_DEV));
        HCI_DBG("LSR %08x interrupt", reg_val);
        if (reg_val & RUART_LINE_STATUS_ERR_OVERRUN)
            HCI_DBG("LSR over run interrupt");

        if (reg_val & RUART_LINE_STATUS_ERR_PARITY)
            HCI_DBG("LSR parity error interrupt");

        if (reg_val & RUART_LINE_STATUS_ERR_FRAMING)
            HCI_DBG("LSR frame error (stop bit error) interrupt");

        if (reg_val & RUART_LINE_STATUS_ERR_BREAK)
            HCI_DBG("LSR break error interrupt");
        break;
    default:
        HCI_DBG("Unknown interrupt type %u", int_id);
        break;
    }

    return 0;
}

static uint16_t amebad_uart_send(uint8_t *buf, uint16_t len)
{
    if (!amebad_uart->tx_run)
        return 0;

#if 0
    UART_SendData(HCI_UART_DEV, buf, len);
    if (tx_cb) tx_cb();
    return len;
#else
    if (amebad_uart->ier & RUART_IER_ETBEI)
    {
        HCI_ERR("Transmitter FIFO empty interrupt has been enabled");
        return 0;
    }

    amebad_uart->tx_buf = buf;
    amebad_uart->tx_len = len;

    /* Enable TX irq to send */
    amebad_uart->ier |= RUART_IER_ETBEI;
    UART_INTConfig(HCI_UART_DEV, RUART_IER_ETBEI, ENABLE);
#endif

    if (amebad_uart->tx_done_sem) {
        if (osif_sem_take(amebad_uart->tx_done_sem, 0xFFFFFFFF) == false) {
            HCI_ERR("amebad_uart->tx_done_sem take fail!");
            return 0;
        }
    }

    return len;
}

static uint16_t amebad_uart_read(uint8_t *buf, uint16_t len)
{
    uint16_t read_len = amebad_uart_rx_to_read_space();
    read_len = (read_len > len) ? len : read_len;

    if (0 == read_len)
        return 0;

    if (read_len > amebad_uart->ring_buffer_size - amebad_uart->read_ptr)
        read_len = amebad_uart->ring_buffer_size - amebad_uart->read_ptr;

    memcpy(buf, &amebad_uart->ring_buffer[amebad_uart->read_ptr], read_len);
    amebad_uart->read_ptr += read_len;
    amebad_uart->read_ptr %= amebad_uart->ring_buffer_size;

    if (amebad_uart->rx_disabled && amebad_uart_rx_to_read_space() < HCI_UART_RX_ENABLE_SIZE)
    {
        UART_INTConfig(HCI_UART_DEV, RUART_IER_ERBI | RUART_IER_ETOI, ENABLE);
        amebad_uart->rx_disabled = 0;
        HCI_INFO("amebad_uart rx enable!");
    }

    return read_len;
}

static void amebad_uart_set_irq(uint8_t trx_flag, uint8_t en)
{
    switch (trx_flag) {
        case UART_RX:
            UART_INTConfig(HCI_UART_DEV, RUART_IER_ERBI | RUART_IER_ETOI, en);
            break;
        case UART_TX:
            UART_INTConfig(HCI_UART_DEV, RUART_IER_ETBEI, en);
            break;
        case UART_TRX:
            UART_INTConfig(HCI_UART_DEV, RUART_IER_ETBEI, en);
            UART_INTConfig(HCI_UART_DEV, RUART_IER_ERBI | RUART_IER_ETOI, en);
            break;
        default:
            break;
    }
}

static uint8_t amebad_uart_open(void)
{
    /* Init amebad_uart */
    if (!amebad_uart)
    {
        amebad_uart = osif_mem_alloc(0, sizeof(struct amebad_uart_t));
        if (!amebad_uart) {
            HCI_ERR("amebad_uart is NULL!");
            return HCI_FAIL;
        }
        memset(amebad_uart, 0, sizeof(struct amebad_uart_t));
    }
    if (!amebad_uart->ring_buffer)
    {
        amebad_uart->ring_buffer = osif_mem_aligned_alloc(0, HCI_UART_RX_BUF_SIZE, 4);
        if (!amebad_uart->ring_buffer) {
            HCI_ERR("amebad_uart->ring_buffer is NULL!");
            return HCI_FAIL;
        }
        memset(amebad_uart->ring_buffer, 0, sizeof(HCI_UART_RX_BUF_SIZE));
    }
    amebad_uart->ring_buffer_size = HCI_UART_RX_BUF_SIZE;
    amebad_uart->read_ptr = 0;
    amebad_uart->write_ptr = 0;
    amebad_uart->rx_disabled = 0;
    amebad_uart->rx_ind = 0;
    amebad_uart->tx_run = 1;
    if (osif_sem_create(&amebad_uart->tx_done_sem, 0, 1) == false) {
        HCI_ERR("amebad_uart->tx_done_sem create fail!");
        return HCI_FAIL;
    }

#ifdef HCI_UART_OUT
    /* Pinmux the Pin for UART */
    Pinmux_Config(HCI_TX_PIN, PINMUX_FUNCTION_UART);
    Pinmux_Config(HCI_RX_PIN, PINMUX_FUNCTION_UART);
    Pinmux_Config(HCI_RTS_PIN, PINMUX_FUNCTION_UART_RTSCTS);
    Pinmux_Config(HCI_CTS_PIN, PINMUX_FUNCTION_UART_RTSCTS);
    PAD_PullCtrl(HCI_TX_PIN, GPIO_PuPd_UP);
    PAD_PullCtrl(HCI_RX_PIN, GPIO_PuPd_NOPULL);
#endif

    /* Init UART_Struct and init UART */
    UART_InitTypeDef *pUARTStruct = &amebad_uart->init_struct;
    UART_StructInit(pUARTStruct);
    pUARTStruct->WordLen = RUART_WLS_8BITS;
    pUARTStruct->StopBit = RUART_STOP_BIT_1;
    pUARTStruct->Parity = RUART_PARITY_DISABLE;
    pUARTStruct->ParityType = RUART_EVEN_PARITY;
    pUARTStruct->StickParity = RUART_STICK_PARITY_DISABLE;
    pUARTStruct->RxFifoTrigLevel = UART_RX_FIFOTRIG_LEVEL_14BYTES;
    /* UART auto-flow control, When the data in UART FIFO
     * reaches rx level, RTS will be pulled high
     */
    pUARTStruct->FlowControl = ENABLE;
    UART_Init(HCI_UART_DEV, pUARTStruct);
    UART_SetBaud(HCI_UART_DEV, 115200);

    /* Init UART irq */
    InterruptDis(HCI_UART_IRQ);
    InterruptUnRegister(HCI_UART_IRQ);
    InterruptRegister((IRQ_FUN)amebad_uart_irq, HCI_UART_IRQ, (uint32_t)amebad_uart, HCI_UART_IRQ_PRIO);
    InterruptEn(HCI_UART_IRQ, HCI_UART_IRQ_PRIO);
    amebad_uart->ier = RUART_IER_ERBI | RUART_IER_ETOI | RUART_IER_ELSI;
    UART_INTConfig(HCI_UART_DEV, RUART_IER_ERBI | RUART_IER_ETOI | RUART_IER_ELSI, ENABLE);
    UART_RxCmd(HCI_UART_DEV, ENABLE);

    return HCI_SUCCESS;
}

static uint8_t amebad_uart_close(void)
{
    if (!amebad_uart)
    {
        HCI_ERR("amebad_uart is NULL!");
        return HCI_FAIL;
    }

    /* Disable UART */
    UART_DeInit(HCI_UART_DEV);
    InterruptDis(HCI_UART_IRQ);
    InterruptUnRegister(HCI_UART_IRQ);

    return HCI_SUCCESS;
}

static uint8_t amebad_uart_free(void)
{
    if (!amebad_uart)
    {
        HCI_ERR("amebad_uart is NULL!");
        return HCI_FAIL;
    }

    if (amebad_uart->tx_done_sem)
        osif_sem_delete(amebad_uart->tx_done_sem);

    /* Free buf */
    if (amebad_uart->ring_buffer)
        osif_mem_aligned_free(amebad_uart->ring_buffer);

    osif_mem_free(amebad_uart);
    amebad_uart = NULL;

    return HCI_SUCCESS;
}

HCI_UART_OPS hci_uart_ops = {
    .open          = amebad_uart_open,
    .close         = amebad_uart_close,
    .free_ops      = amebad_uart_free,
    .send          = amebad_uart_send,
    .read          = amebad_uart_read,
    .set_rx_ind    = amebad_uart_set_rx_ind,
    .set_tx_run    = amebad_uart_set_tx_run,
    .set_bdrate    = amebad_uart_set_bdrate,
    .set_irq       = amebad_uart_set_irq,
    .bridge_open   = amebad_uart_bridge_open,
    .bridge_to_hci = amebad_uart_bridge_to_hci,
};

void set_hci_uart_out(bool flag)
{
    hci_uart_bridge_open(flag);
}

bool hci_uart_tx_bridge(uint8_t rc)
{
    hci_uart_bridge_to_hci(rc);
}
