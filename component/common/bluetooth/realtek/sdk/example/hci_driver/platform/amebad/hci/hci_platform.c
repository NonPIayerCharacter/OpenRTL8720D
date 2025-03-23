/*
 *******************************************************************************
 * Copyright(c) 2022, Realtek Semiconductor Corporation. All rights reserved.
 *******************************************************************************
 */

#include "osif.h"
#include "hci/hci_common.h"
#include "hci_platform.h"
#include "hci_dbg.h"
#include "bt_intf.h"
#include "wifi_conf.h"
#include "build_info.h"

#define READ_SW(x)               (x = HAL_READ32(SPI_FLASH_BASE, FLASH_SYSTEM_DATA_ADDR + 0x28))

#define HCI_LGC_EFUSE_BASE       0x190
#define HCI_PHY_EFUSE_BASE1      0x120
#define HCI_PHY_EFUSE_BASE2      0x1FD
#define HCI_LGC_EFUSE_LEN        0x20
#define HCI_PHY_EFUSE_LEN        0x13
#define LEFUSE(x)                (x - HCI_LGC_EFUSE_BASE)

#define HCI_CFG_BAUDRATE          BIT0
#define HCI_CFG_FLOWCONTROL       BIT1
#define HCI_CFG_BD_ADDR           BIT2
#define HCI_MAC_ADDR_LEN          6

#define HCI_CONFIG_HDR_LEN        6
#define HCI_CONFIG_SIGNATURE      0x8723ab55
#define MERGE_PATCH_ADDRESS_OTA1  0x080F8000
#define MERGE_PATCH_ADDRESS_OTA2  0x081F8000
#define MERGE_PATCH_SWITCH_ADDR   0x08003028
#define MERGE_PATCH_SWITCH_SINGLE 0xAAAAAAAA
#define HCI_PATCH_FRAG_SIZE       252

#define LE_ARRAY_TO_UINT16(u16, a)          \
    {                                       \
        u16 = ((uint16_t)(*(a + 0)) << 0) + \
              ((uint16_t)(*(a + 1)) << 8);  \
    }

#define LE_ARRAY_TO_UINT32(u32, a)           \
    {                                        \
        u32 = ((uint32_t)(*(a + 0)) << 0) +  \
              ((uint32_t)(*(a + 1)) << 8) +  \
              ((uint32_t)(*(a + 2)) << 16) + \
              ((uint32_t)(*(a + 3)) << 24);  \
    }

#define LE_UINT32_TO_ARRAY(a, u32)                      \
    {                                                   \
        *((uint8_t *)(a) + 0) = (uint8_t)((u32) >> 0);  \
        *((uint8_t *)(a) + 1) = (uint8_t)((u32) >> 8);  \
        *((uint8_t *)(a) + 2) = (uint8_t)((u32) >> 16); \
        *((uint8_t *)(a) + 3) = (uint8_t)((u32) >> 24); \
    }

#define hci_board_32reg_set(addr, val) HAL_WRITE32(addr, 0, val)
#define hci_board_32reg_read(addr) HAL_READ32(addr, 0)

typedef struct {
	u32 IQK_xx;
	u32 IQK_yy;
	u32 IDAC;
	u32 QDAC;
} BT_Cali_TypeDef;

typedef struct {
    uint8_t *fw_buf;
    uint8_t fw_is_alloced;
    uint16_t fw_len;
    uint8_t *config_buf;
    uint8_t cfg_is_alloced;
    uint16_t config_len;
    uint16_t cur_index;
    uint16_t end_index;
    uint8_t last_pkt;
    uint32_t sent_len;
} HCI_PATCH_INFO;

static uint8_t hci_lgc_efuse[HCI_LGC_EFUSE_LEN]  = {0};
static uint8_t hci_phy_efuse[HCI_PHY_EFUSE_LEN]  = {0};
static BT_Cali_TypeDef iqk_data;
static uint8_t hci_chipid_in_fw = 0;
static uint8_t hci_cfg_bd_addr[HCI_MAC_ADDR_LEN] = {0};
static uint32_t hci_cfg_init_uart_baudrate       = 115200;
static uint32_t hci_cfg_work_uart_baudrate       = 0;
static uint8_t  hci_cfg_work_bt_baudrate[4]      = {0};
static uint8_t hci_init_config[] = {
	0x55, 0xab, 0x23, 0x87,                               /* header */
	0x19, 0x00,                                           /* Config length: header + len + preload */
	0x30, 0x00, 0x06, 0x11, 0x28, 0x36, 0x12, 0x51, 0x89, /* BT MAC address */
	0x0c, 0x00, 0x04, 0x04, 0x50, 0xF7, 0x03,             /* Baudrate 921600 */
	0x18, 0x00, 0x01, 0x5c,                               /* flow control */
	0x94, 0x01, 0x06, 0x08, 0x00, 0x00, 0x00, 0x27, 0x07, /* efuse value */
	0x9f, 0x01, 0x05, 0x23, 0x23, 0x23, 0x23, 0x59,
	0xA4, 0x01, 0x04, 0xFE, 0xFE, 0xFE, 0xFE,
};
static HCI_PATCH_INFO* hci_patch_info = NULL;

uint32_t hci_sw_val = 0xffffffff;
HCI_IQK_DATA hci_iqk_data[HCI_START_IQK_TIMES] = {
    {0x00, 0x4000}, {0x01, 0x0180}, {0x02, 0x3800}, {0x3f, 0x0400},
};

extern uint32_t bt_iqk_8721d(BT_Cali_TypeDef *cal_data, uint8_t store);
extern uint32_t bt_lok_write(uint32_t lok_xx, uint32_t lok_yy);
extern uint32_t bt_dck_write(uint32_t q_dck, uint32_t i_dck);
extern uint32_t bt_flatk_8721d(uint16_t txgain_flatk);
static uint8_t hci_platform_parse_config(void);

/******************************************************************************
 * IQK About
 */
static void bt_dump_iqk(BT_Cali_TypeDef *iqk_data)
{
    if (!CHECK_SW(EFUSE_SW_DRIVER_DEBUG_LOG))
    {
        HCI_PRINT("bt_iqk_dump: \n\r");
        HCI_PRINT("the IQK_xx - data is 0x%x\n\r", iqk_data->IQK_xx);
        HCI_PRINT("the IQK_yy - data is 0x%x\n\r", iqk_data->IQK_yy);
        HCI_PRINT("the QDAC   - data is 0x%x\n\r", iqk_data->QDAC);
        HCI_PRINT("the IDAC   - data is 0x%x\n\r", iqk_data->IDAC);
    }
}

static uint8_t bt_iqk_logic_efuse_valid(BT_Cali_TypeDef *bt_iqk_data)
{
    if (((hci_lgc_efuse[0x16] == 0xff) && (hci_lgc_efuse[0x17] == 0xff)) ||
        ((hci_lgc_efuse[0x18] == 0xff) && (hci_lgc_efuse[0x19] == 0xff)) ||
        (hci_lgc_efuse[0x1a] == 0xff) || (hci_lgc_efuse[0x1b] == 0xff))
    {
        HCI_ERR("lgc_efuse: no data\r\n");
        return HCI_FAIL;
    }

    bt_iqk_data->IQK_xx = hci_lgc_efuse[0x16] | hci_lgc_efuse[0x17] << 8;
    bt_iqk_data->IQK_yy = hci_lgc_efuse[0x18] | hci_lgc_efuse[0x19] << 8;
    bt_iqk_data->QDAC = hci_lgc_efuse[0x1a];
    bt_iqk_data->IDAC = hci_lgc_efuse[0x1b];

    HCI_DBG("lgc_efuse: has data\r\n");
    return HCI_SUCCESS;
}

static uint8_t bt_iqk_efuse_valid(BT_Cali_TypeDef *bt_iqk_data)
{
    if ((hci_phy_efuse[1] & BIT0))
    {
        HCI_ERR("phy_efuse: no data");
        return HCI_FAIL;
    }

    bt_iqk_data->IQK_xx = hci_phy_efuse[3] | hci_phy_efuse[4] << 8;
    bt_iqk_data->IQK_yy = hci_phy_efuse[5] | hci_phy_efuse[6] << 8;
    bt_iqk_data->QDAC = hci_phy_efuse[0x0c];
    bt_iqk_data->IDAC = hci_phy_efuse[0x0d];

    HCI_DBG("phy_efuse: has data hci_phy_efuse[1]= %x", hci_phy_efuse[1]);
    return HCI_SUCCESS;
}

#if 0
static void bt_write_lgc_efuse_value(void)
{
	hci_lgc_efuse[0x16] = iqk_data.IQK_xx & 0xff;
	hci_lgc_efuse[0x17] = (iqk_data.IQK_xx >> 8) & 0xff;
	hci_lgc_efuse[0x18] = iqk_data.IQK_yy & 0xff;
	hci_lgc_efuse[0x19] = (iqk_data.IQK_yy >> 8) & 0xff;
	hci_lgc_efuse[0x1a] = iqk_data.QDAC;
	hci_lgc_efuse[0x1b] = iqk_data.IDAC;

	hci_board_debug("\r\n write logic efuse 0x1A6 =0x%02x", hci_lgc_efuse[0x16]);
	hci_board_debug("\r\n write logic efuse 0x1A7 =0x%02x", hci_lgc_efuse[0x17]);
	hci_board_debug("\r\n write logic efuse 0x1A8 =0x%02x", hci_lgc_efuse[0x18]);
	hci_board_debug("\r\n write logic efuse 0x1A9 =0x%02x", hci_lgc_efuse[0x19]);
	hci_board_debug("\r\n write logic efuse 0x1Aa =0x%02x", hci_lgc_efuse[0x1a]);
	hci_board_debug("\r\n write logic efuse 0x1Ab =0x%02x", hci_lgc_efuse[0x1b]);
	//EFUSE_LMAP_WRITE(0x1A4,8,(uint8_t *)&hci_lgc_efuse[0x14]);
}
#endif

uint8_t hci_platform_check_iqk(void)
{
    BT_Cali_TypeDef bt_iqk_data;

    if (!(hci_lgc_efuse[LEFUSE(0x1A1)] & BIT0))
    {
        HCI_DBG("\r\nUse fix logic efuse");

        if (HCI_SUCCESS == bt_iqk_logic_efuse_valid(&bt_iqk_data))
        {
            bt_dump_iqk(&bt_iqk_data);
            bt_lok_write(bt_iqk_data.IDAC, bt_iqk_data.QDAC);
            hci_phy_efuse[0] = 0;
            hci_phy_efuse[1] = hci_phy_efuse[1] & (~BIT0);
            // hci_phy_efuse[1] = 0xfe;
            // hci_phy_efuse[2] = 0xff;
            hci_phy_efuse[3] = bt_iqk_data.IQK_xx & 0xff;
            hci_phy_efuse[4] = (bt_iqk_data.IQK_xx >> 8) & 0xff;
            hci_phy_efuse[5] = bt_iqk_data.IQK_yy & 0xff;
            hci_phy_efuse[6] = (bt_iqk_data.IQK_yy >> 8) & 0xff;

            return HCI_SUCCESS;
        }

        HCI_ERR("\r\nlogic efuse has NO data");
        return HCI_FAIL;
    }

    if (HCI_SUCCESS == bt_iqk_efuse_valid(&bt_iqk_data))
    {
        if (hci_phy_efuse[0] != 0)
            bt_dck_write(hci_phy_efuse[0x0e], hci_phy_efuse[0x0f]);
        else
            HCI_DBG("\r\nhci_phy_efuse[0]=0");
        bt_dump_iqk(&bt_iqk_data);
        bt_lok_write(bt_iqk_data.IDAC, bt_iqk_data.QDAC);
    }
    else if (HCI_SUCCESS == bt_iqk_logic_efuse_valid(&bt_iqk_data))
    {
        bt_dump_iqk(&bt_iqk_data);
        bt_lok_write(bt_iqk_data.IDAC, bt_iqk_data.QDAC);
        hci_phy_efuse[0] = 0;
        hci_phy_efuse[1] = hci_phy_efuse[1] & (~BIT0);
        // hci_phy_efuse[1] = 0xfe;
        // hci_phy_efuse[2] = 0xff;
        hci_phy_efuse[3] = bt_iqk_data.IQK_xx & 0xff;
        hci_phy_efuse[4] = (bt_iqk_data.IQK_xx >> 8) & 0xff;
        hci_phy_efuse[5] = bt_iqk_data.IQK_yy & 0xff;
        hci_phy_efuse[6] = (bt_iqk_data.IQK_yy >> 8) & 0xff;
    }
    else
    {
        HCI_ERR("NO IQK LOK DATA, need start LOK");
        // bt_change_gnt_wifi_only();
        // bt_adda_dck_8721d();
        // reset_iqk_type();
        return HCI_FAIL;
    }

    return HCI_SUCCESS;
}

uint8_t hci_platform_start_iqk(void)
{
    u32 ret = 0;
    // bt_change_gnt_wifi_only();

    /* JUST FOR DEBUG */
    if (rltk_wlan_is_mp())
    {
        HCI_DBG("BT GNT_BT %x LOG...\n", HAL_READ32(0x40080000, 0x0764));

        ret = bt_iqk_8721d(&iqk_data, 0);
        bt_dump_iqk(&iqk_data);

        if (!CHECK_SW(EFUSE_SW_BT_FW_LOG))
        {
            HCI_PRINT(" Please write logic efuse 0x1A6 =0x%02x\n\r", iqk_data.IQK_xx & 0xff);
            HCI_PRINT(" Please write logic efuse 0x1A7 =0x%02x\n\r", (iqk_data.IQK_xx >> 8) & 0xff);
            HCI_PRINT(" Please write logic efuse 0x1A8 =0x%02x\n\r", iqk_data.IQK_yy & 0xff);
            HCI_PRINT(" Please write logic efuse 0x1A9 =0x%02x\n\r", (iqk_data.IQK_yy >> 8) & 0xff);
            HCI_PRINT(" Please write logic efuse 0x1AA =0x%02x\n\r", iqk_data.QDAC);
            HCI_PRINT(" Please write logic efuse 0x1AB =0x%02x\n\r", iqk_data.IDAC);
        }
    }
    else
    {
        ret = bt_iqk_8721d(&iqk_data, 0);
        bt_dump_iqk(&iqk_data);
    }

    if (_FAIL == ret)
    {
        HCI_ERR("IQK Fail, please connect driver!\r\n");
        return HCI_FAIL;
    }

    bt_lok_write(iqk_data.IDAC, iqk_data.QDAC);

    hci_phy_efuse[0] = 0;
    hci_phy_efuse[1] = hci_phy_efuse[1] & (~BIT0);
    // hci_phy_efuse[1] = 0xfe;
    // hci_phy_efuse[2] = 0xff;
    hci_phy_efuse[3] = iqk_data.IQK_xx & 0xff;
    hci_phy_efuse[4] = (iqk_data.IQK_xx >> 8) & 0xff;
    hci_phy_efuse[5] = iqk_data.IQK_yy & 0xff;
    hci_phy_efuse[6] = (iqk_data.IQK_yy >> 8) & 0xff;
    // bt_write_lgc_efuse_value();

    return HCI_SUCCESS;
}

uint8_t hci_platform_get_iqk_data(uint8_t *data, uint8_t len)
{
    memcpy(data, hci_phy_efuse, len);
    return HCI_SUCCESS;
}

/******************************************************************************
 * ROM Version About
 */
void hci_platform_record_chipid(uint8_t chipid)
{
    hci_chipid_in_fw = chipid;

    /* Just Config parse here, left find patch later */
    hci_platform_parse_config();
}

/******************************************************************************
 * Baudrate About
 */
static void hci_platform_convert_baudrate(uint32_t *bt_baudrate, uint32_t *uart_baudrate, uint8_t bt_to_uart)
{
    uint8_t i;

    const struct {
        uint32_t bt_baudrate;
        uint32_t uart_baudrate;
    } baudrate_map[] = {
        {0x0000701d, 115200},
        {0x0252C00A, 230400},
        {0x03F75004, 921600},
        {0x05F75004, 921600},
        {0x00005004, 1000000},
        {0x04928002, 1500000},
        {0x00005002, 2000000},
        {0x0000B001, 2500000},
        {0x04928001, 3000000},
        {0x052A6001, 3500000},
        {0x00005001, 4000000},
    };

    const uint32_t baudrate_map_len = sizeof(baudrate_map)/sizeof(baudrate_map[0]);

    if (bt_to_uart)
    {
        for (i = 0; i < baudrate_map_len; i++)
        {
            if (*bt_baudrate == baudrate_map[i].bt_baudrate)
                break;
        }

        if (i == baudrate_map_len) {
            HCI_ERR("Wrong Baudrate Selection! Use Default 115200!");
            i = 0;
        }
        *uart_baudrate = baudrate_map[i].uart_baudrate;
    }
    else
    {
        for (i = 0; i < baudrate_map_len; i++)
        {
            if (*uart_baudrate == baudrate_map[i].uart_baudrate)
                break;
        }

        if (i == baudrate_map_len) {
            HCI_ERR("Wrong Baudrate Selection! Use Default 115200!");
            i = 0;
        }
        *bt_baudrate = baudrate_map[i].bt_baudrate;
    }
}

void hci_platform_get_baudrate(uint8_t *baudrate, uint8_t len)
{
    /* memcpy */
    for (uint8_t i = 0; i < len; i++)
        baudrate[i] = hci_cfg_work_bt_baudrate[i];
}

uint8_t hci_platform_set_baudrate(void)
{
    hci_uart_set_bdrate(hci_cfg_work_uart_baudrate);
    osif_delay(10);

    return HCI_SUCCESS;
}

/******************************************************************************
 * Config and Patch About
 */
static uint8_t hci_platform_parse_config(void)
{
    uint8_t *p, *p_len, i;
    uint16_t actual_len, entry_offset, entry_len;
    uint16_t tx_flatk;

    if (sizeof(hci_init_config) <= HCI_CONFIG_HDR_LEN)
        return HCI_IGNORE;

    p = hci_init_config;
    if (HCI_CONFIG_SIGNATURE != *(uint32_t*)(p))
    {
        HCI_ERR("invalid signature 0x%08x", *(uint32_t*)(p));
        return HCI_FAIL;
    }

    p_len = hci_init_config + 4;
    actual_len = sizeof(hci_init_config) - HCI_CONFIG_HDR_LEN;
    /* Fix the len, just avoid the length is not correct */
    if (*(uint16_t *)p_len != actual_len)
        *(uint16_t *)p_len = actual_len;

    p += HCI_CONFIG_HDR_LEN;
    while (p < hci_init_config + sizeof(hci_init_config))
    {
        entry_offset = *(uint16_t*)(p);
        entry_len = *(uint8_t*)(p + 2);
        p += 3;

        switch (entry_offset)
        {
        case 0x000c:
            /* MP Mode, Use Default: 115200 */
            if ((rltk_wlan_is_mp()) || (!CHECK_SW(EFUSE_SW_UPPERSTACK_SWITCH)))
                hci_platform_convert_baudrate((uint32_t *)p, &hci_cfg_init_uart_baudrate, 0);

            hci_platform_convert_baudrate((uint32_t *)p, &hci_cfg_work_uart_baudrate, 1);
            hci_platform_convert_baudrate((uint32_t *)hci_cfg_work_bt_baudrate, &hci_cfg_work_uart_baudrate, 0);
            break;
        case 0x0018:
            /* MP Mode, Close Flow Control */
            if ((rltk_wlan_is_mp()) || (!CHECK_SW(EFUSE_SW_UPPERSTACK_SWITCH)))
            {
                p[0] = p[0] & (~BIT2);
                HCI_DBG("close hci uart flow ctrl: 0x%02x", p[0]);
            }
            break;
        case 0x0030:
            if (entry_len != HCI_MAC_ADDR_LEN)
            {
                HCI_DBG("wrong mac addr len in hci config_buf, check it!");
                break;
            }

            /* Set ConfigBuf MacAddr */
            if ((hci_lgc_efuse[0] != 0xff) || (hci_lgc_efuse[1] != 0xff) || (hci_lgc_efuse[2] != 0xff) ||
                (hci_lgc_efuse[3] != 0xff) || (hci_lgc_efuse[4] != 0xff) || (hci_lgc_efuse[5] != 0xff))
            {
                for (i = 0; i < HCI_MAC_ADDR_LEN; i++)
                    p[i] = hci_lgc_efuse[HCI_MAC_ADDR_LEN - 1 - i];
            }
            HCI_PRINT("Bluetooth init BT_ADDR in cfgbuf [%02x:%02x:%02x:%02x:%02x:%02x]\n\r",
                        p[5], p[4], p[3], p[2], p[1], p[0]);
            break;
        case 0x194:
            if (hci_lgc_efuse[LEFUSE(0x196)] == 0xff)
            {
                if (!(hci_phy_efuse[2] & BIT0))
                {
                    tx_flatk = hci_phy_efuse[0x0a] | hci_phy_efuse[0x0b] << 8;
                    bt_flatk_8721d(tx_flatk);
                    HCI_DBG("Write physical FLATK=tx_flatk=%x", tx_flatk);
                }
                break;
            }
            else
            {
                p[0] = hci_lgc_efuse[LEFUSE(0x196)];
                if (hci_lgc_efuse[LEFUSE(0x196)] & BIT1)
                    p[1] = hci_lgc_efuse[LEFUSE(0x197)];

                if (hci_lgc_efuse[LEFUSE(0x196)] & BIT2)
                {
                    p[2] = hci_lgc_efuse[LEFUSE(0x198)];
                    p[3] = hci_lgc_efuse[LEFUSE(0x199)];
                    tx_flatk = hci_lgc_efuse[LEFUSE(0x198)] | hci_lgc_efuse[LEFUSE(0x199)] << 8;
                    bt_flatk_8721d(tx_flatk);
                    HCI_DBG("Write logical FLATK=tx_flatk=%x", tx_flatk);
                }
                else
                {
                    if (!(hci_phy_efuse[2] & BIT0))
                    {
                        tx_flatk = hci_phy_efuse[0xa] | hci_phy_efuse[0xb] << 8;
                        bt_flatk_8721d(tx_flatk);
                        HCI_DBG("Write physical FLATK=tx_flatk=%x", tx_flatk);
                    }
                }

                if (hci_lgc_efuse[LEFUSE(0x196)] & BIT5)
                {
                    p[4] = hci_lgc_efuse[LEFUSE(0x19a)];
                    p[5] = hci_lgc_efuse[LEFUSE(0x19b)];
                }
            }
            break;
        case 0x19f:
            for (i = 0; i < entry_len; i++)
            {
                if (hci_lgc_efuse[LEFUSE(0x19c + i)] != 0xff)
                    p[i] = hci_lgc_efuse[LEFUSE(0x19c + i)];
            }
            break;
        case 0x1A4:
            for (i = 0; i < entry_len; i++)
            {
                if (hci_lgc_efuse[LEFUSE(0x1a2 + i)] != 0xff)
                    p[i] = hci_lgc_efuse[LEFUSE(0x1A2 + i)];
            }
            break;
        default:
			HCI_ERR("Unknown hci config_buf entry offset: 0x%04x, len: 0x%02x", entry_offset, entry_len);
            break;
        }

        p +=  entry_len;
    }

    return HCI_SUCCESS;
}

static uint8_t *hci_platform_find_patch_address(void)
{
    //if (CHECK_SW(EFUSE_SW_USE_FLASH_PATCH))
    //{
        return (uint8_t *)rltk_bt_get_patch_code();
    //}
    //else if (ota_get_cur_index() == OTA_INDEX_1)
    //{
    //    HCI_DBG("We use BT ROM OTA2 PATCH ADDRESS:0x%x", MERGE_PATCH_ADDRESS_OTA2);
    //    return (uint8_t *)MERGE_PATCH_ADDRESS_OTA2;
    //}
    //else
    //{
    //    HCI_DBG("\nWe use BT ROM OTA1 PATCH ADDRESS:0x%x\n", MERGE_PATCH_ADDRESS_OTA1);
    //    return (uint8_t *)MERGE_PATCH_ADDRESS_OTA1;
    //}
}

// extern unsigned int rtlbt_fw_len;
static uint8_t hci_platform_get_patch_info(void)
{
    HCI_PATCH_INFO *patch_info = hci_patch_info;
    const uint8_t no_patch_sig[] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint16_t num_of_patch, fw_chip_id, fw_len, i;
    uint32_t fw_offset, svn_version, coex_version, lmp_subversion;

    /* Not same as original code, we use patch_info->fw_buf to porint to 
     * FW, only thing we need consider is setting 'lmp_subversion' at end!
     */
    patch_info->fw_buf = hci_platform_find_patch_address();

    if (!memcmp(patch_info->fw_buf, "Realtech", sizeof("Realtech")-1))
    {
        /* Follow origin code, no check to fw_id? */
        HCI_DBG("Use merged patch");
        LE_ARRAY_TO_UINT32(lmp_subversion, patch_info->fw_buf + 8);
        LE_ARRAY_TO_UINT16(num_of_patch, patch_info->fw_buf + 12);

		if (1 == num_of_patch) {
            LE_ARRAY_TO_UINT16(fw_len, patch_info->fw_buf + 0x0e + 2 * num_of_patch);
            LE_ARRAY_TO_UINT32(fw_offset, patch_info->fw_buf + 0x0e + 4 * num_of_patch);
			if (rltk_wlan_is_mp())
            {
				HCI_DBG("fw_chip_id patch =%x,num_of_patch=%x", fw_chip_id, num_of_patch);
				HCI_DBG("lmp_subversion=%x , fw_len =%x, fw_offset = %x", lmp_subversion, fw_len, fw_offset);
			}
		}
        else
        {
            for (i = 0; i < num_of_patch; i++)
            {
                LE_ARRAY_TO_UINT16(fw_chip_id, patch_info->fw_buf + 0x0e + 2 * i);
                if (fw_chip_id == hci_chipid_in_fw)
                {
                    LE_ARRAY_TO_UINT16(fw_len, patch_info->fw_buf + 0x0e + 2 * num_of_patch + 2 * i);
                    LE_ARRAY_TO_UINT32(fw_offset, patch_info->fw_buf + 0x0e + 4 * num_of_patch + 4 * i);
                    break;
                }
            }

            if (i >= num_of_patch)
            {
                HCI_ERR("Use normal patch but no match patch");
                return HCI_FAIL;
            }
        }

        patch_info->fw_buf = patch_info->fw_buf + fw_offset;
        /* LE_UINT32_TO_ARRAY(patch_info->fw_buf + fw_len - 4, lmp_subversion); */
        patch_info->fw_len = fw_len;
        patch_info->fw_is_alloced = 0;
    }
    else if (!memcmp(patch_info->fw_buf, no_patch_sig, sizeof(no_patch_sig)))
    {
        HCI_WARN("NO patch!");
        return HCI_IGNORE;
    }
    else
    {
        HCI_DBG("Use single patch");
		if (patch_info->fw_buf != (uint8_t *)rltk_bt_get_patch_code())
        {
			HCI_ERR("not support single patch on rom");
			return false;
		}

        patch_info->fw_len = rltk_bt_get_patch_code_len(); /* rtlbt_fw_len? */
        patch_info->fw_is_alloced = 0;
    }

CONFIG_PATCH_INFO:
    /* Set config info */
    patch_info->config_buf = hci_init_config;
    patch_info->config_len = sizeof(hci_init_config);

    /* Calculate patch info */
    patch_info->end_index = (patch_info->fw_len + patch_info->config_len - 1) / HCI_PATCH_FRAG_SIZE;
    patch_info->last_pkt = (patch_info->fw_len + patch_info->config_len) % HCI_PATCH_FRAG_SIZE;
    if (patch_info->last_pkt == 0)
        patch_info->last_pkt = HCI_PATCH_FRAG_SIZE;

    LE_ARRAY_TO_UINT32(svn_version, (patch_info->fw_buf + patch_info->fw_len - 8));
    LE_ARRAY_TO_UINT32(coex_version, (patch_info->fw_buf + patch_info->fw_len - 12));
    HCI_DBG("Use svn_version=0x%x, coex_version=0x%x, lmp_subversion=0x%x, fw_buf=0x%x, "
            "fw_len=0x%x, config_buf=0x%x, config_len=0x%x, baudrate=0x%x",
            svn_version, coex_version, lmp_subversion, patch_info->fw_buf, patch_info->fw_len, 
            patch_info->config_buf, patch_info->config_len, hci_cfg_work_uart_baudrate);

    return HCI_SUCCESS;
}

uint8_t hci_platform_dl_patch_init(void)
{
    hci_patch_info = osif_mem_alloc(0, sizeof(HCI_PATCH_INFO));
    if (!hci_patch_info)
        return HCI_FAIL;

    memset(hci_patch_info, 0, sizeof(HCI_PATCH_INFO));

    return HCI_SUCCESS;
}

uint8_t hci_platform_get_patch_cmd_len(uint8_t *cmd_len)
{
	uint8_t ret;
    HCI_PATCH_INFO *patch_info = hci_patch_info;

    /* Download FW partial patch first time, get patch and info */
    if (0 == patch_info->cur_index)
    {
        if (HCI_SUCCESS != hci_platform_get_patch_info())
            return HCI_FAIL;
    }

    if (patch_info->cur_index == patch_info->end_index)
    {
        *cmd_len = patch_info->last_pkt + 1;
        return HCI_SUCCESS;
    }

    *cmd_len = HCI_PATCH_FRAG_SIZE + 1;

    return HCI_SUCCESS;
}

uint8_t hci_platform_get_patch_cmd_buf(uint8_t *cmd_buf, uint8_t cmd_len)
{
    HCI_PATCH_INFO* patch_info = hci_patch_info;
    uint8_t*        data_buf   = &cmd_buf[1];
    uint8_t         data_len   = cmd_len - 1;

    /* first byte is index */
    if (patch_info->cur_index >= 0x80) {
        cmd_buf[0] = (patch_info->cur_index - 0x80) % 0x7f + 1;
    } else {
        cmd_buf[0] = patch_info->cur_index % 0x80;
    }
    if (patch_info->cur_index == patch_info->end_index)
        cmd_buf[0] |= 0x80;

    if (patch_info->sent_len + data_len <= patch_info->fw_len)
    {
        /* within fw patch domain */
        memcpy(data_buf, patch_info->fw_buf + patch_info->sent_len, data_len);
    }
    else if ((patch_info->sent_len < patch_info->fw_len) && (patch_info->sent_len + data_len > patch_info->fw_len))
    {
        /* need copy fw patch domain and config domain */
        memcpy(data_buf, patch_info->fw_buf + patch_info->sent_len, patch_info->fw_len - patch_info->sent_len);

        memcpy(data_buf + (patch_info->fw_len - patch_info->sent_len), patch_info->config_buf,
               data_len - (patch_info->fw_len - patch_info->sent_len));
    }
    else
    {
        memcpy(data_buf, patch_info->config_buf + (patch_info->sent_len - patch_info->fw_len), data_len);
    }

    patch_info->sent_len += data_len;
    patch_info->cur_index++;

    return HCI_SUCCESS;
}

void hci_platform_dl_patch_done(void)
{
    if (hci_patch_info)
    {
        if(hci_patch_info->fw_buf && hci_patch_info->fw_is_alloced)
            osif_mem_free(hci_patch_info->fw_buf);
        hci_patch_info->fw_buf = NULL;

        osif_mem_free(hci_patch_info);
        hci_patch_info = NULL;
    }
}

/******************************************************************************
 * Init and Deinit About
 */
static uint32_t cal_bit_shift(uint32_t Mask)
{
    uint32_t i;
    for (i = 0; i < 31; i++)
    {
        if (((Mask >> i) & 0x1) == 1)
            break;
    }
    return i;
}

static void set_reg_value(uint32_t reg_address, uint32_t Mask, uint32_t val)
{
	uint32_t shift = 0;
	uint32_t data = 0;

	data = hci_board_32reg_read(reg_address);
	shift = cal_bit_shift(Mask);
	data = ((data & (~Mask)) | (val << shift));
	hci_board_32reg_set(reg_address, data);
	data = hci_board_32reg_read(reg_address);
}

static uint8_t hci_platform_read_efuse(void)
{
    /* Read logic efuse */
    uint8_t *p_buf = osif_mem_alloc(RAM_TYPE_DATA_ON, 1024);
    if (!p_buf || _FAIL == EFUSE_LMAP_READ(p_buf))
    {
        HCI_ERR("Read logic efuse failed!");
        return HCI_FAIL;
    }
    memcpy(hci_lgc_efuse, p_buf + HCI_LGC_EFUSE_BASE, HCI_LGC_EFUSE_LEN);
    osif_mem_free(p_buf);

    /* Read physical efuse */
    for (uint32_t i = 0, j = 0; i < HCI_PHY_EFUSE_LEN; i++)
    {
        if (i < (HCI_PHY_EFUSE_LEN - 3))
        {
            EFUSE_PMAP_READ8(0, HCI_PHY_EFUSE_BASE1 + i, hci_phy_efuse + i, L25EOUTVOLTAGE);
            if ((i == 7) && (hci_phy_efuse[i] == 0))
                hci_phy_efuse[i] = 0x13;
        }
        else
        {
            EFUSE_PMAP_READ8(0, HCI_PHY_EFUSE_BASE2 + j, hci_phy_efuse + i, L25EOUTVOLTAGE);
            j++;
        }
    }

    /* Dump phy_efuse and logic_efuse */
    if (!CHECK_SW(EFUSE_SW_DRIVER_DEBUG_LOG))
    {
        HCI_PRINT("\n\rDump logic efuse:\n\r");
        for (int i = 0; i < 0x20; i++)
            HCI_PRINT("%02x ", hci_lgc_efuse[i]);

        HCI_PRINT("Dump physical efuse (0x120~0x130, 0x1FD~0x1FF): \n\r");
        for (int i = 0; i < 18; i++)
            HCI_PRINT("%02x ", hci_phy_efuse[i]);
        HCI_PRINT("\n\r");
    }

    return HCI_SUCCESS;
}

static inline void bt_power_on(void)
{
    set_reg_value(0x40000000, BIT0 | BIT1, 3);
    osif_delay(5);
}

static inline void bt_power_off(void)
{
    set_reg_value(0x40000000, BIT0 | BIT1, 0);
}

static void bt_reset(void)
{
    HCI_DBG("BT Reset...");

    /* PowerSaving */
    if (!rltk_wlan_is_mp())
        wifi_disable_powersave();

    /* BT FW log debug control */
    if (!CHECK_SW(EFUSE_SW_BT_FW_LOG))
    {
        HCI_DBG("BT FW LOG OPEN, at P_A16\n");
        /* Use command to close: 'EFUSE wmap 1a1 1 fe' */
        set_reg_value(0x48000440, BIT0 | BIT1 | BIT2 | BIT3 | BIT4, 17);
        osif_delay(5);
    }

    /* BT Power on */
    bt_power_on();

    /* Isolation */
    set_reg_value(0x40000000, BIT16, 0);
    osif_delay(5);

    /* BT function enable */
    set_reg_value(0x40000204, BIT24, 0);
    osif_delay(5);
    set_reg_value(0x40000204, BIT24, 1);
    osif_delay(50);

    /* BT clock enable */
    set_reg_value(0x40000214, BIT24, 1);
    osif_delay(5);
}

static inline void hci_normal_start(void)
{
    if (!rltk_wlan_is_mp())
        rltk_coex_bt_enable(1);
}

#ifdef FT_MODE
static inline void bt_change_gnt_wifi_only(void)
{
    set_reg_value(0x40080764, BIT9 | BIT10, 1);
}
#endif

#if 0
static inline void bt_change_gnt_bt_only(void)
{
    set_reg_value(0x40080764, BIT9 | BIT10, 3);
}

static void hci_uart_out(void)
{
	HCI_DBG("HCI UART OUT OK: PA2 TX, PA4 RX");

	HAL_WRITE32(0x48000000, 0x5f0, 0x00000202);
	/* PA2 TX */
	HAL_WRITE32(0x48000000, 0x408, 0x00005C11);
	/* PA4 RX */
	HAL_WRITE32(0x48000000, 0x410, 0x00005C11);

	bt_change_gnt_bt_only();
}
#endif

uint8_t hci_platform_init(void)
{
    READ_SW(hci_sw_val);

    if(!(wifi_is_up(RTW_STA_INTERFACE) || wifi_is_up(RTW_AP_INTERFACE))) 
    {
        HCI_ERR("WiFi is OFF! Please Restart BT after Wifi on!");
        return HCI_FAIL;
    }

    if (rltk_wlan_is_mp())
        HCI_DBG("This is BT MP Driver, AmebaD %x Cut", SYSCFG_CUTVersion() + 10);
    else
        HCI_DBG("This is BT MP Driver, AmebaD %x Cut", SYSCFG_CUTVersion() + 10);

#ifdef UTS_VERSION
    HCI_DBG("BT BUILD Date: %s", UTS_VERSION);
#else
    HCI_DBG("BT BUILD Date: %s, %s", __DATE__, __TIME__);
#endif
    HCI_DBG("BT HCI Debug Val: 0x%x", hci_sw_val);

    /* Read efuse */
    if (HCI_FAIL == hci_platform_read_efuse())
        return HCI_FAIL;

    /* BT Reset */
    bt_reset();

    /* BT Coex */
    hci_normal_start();

    /* BT UART Open */
    if (HCI_FAIL == hci_uart_open())
        return HCI_FAIL;

    return HCI_SUCCESS;
}

uint8_t hci_platform_deinit(void)
{
    /* BT UART Close */
    if (HCI_SUCCESS != hci_uart_close())
        return HCI_FAIL;

    /* Power off */
    bt_power_off();

    /* BT Coex */
    rltk_coex_bt_enable(0);

    /* PowerSaving */
    if (!rltk_wlan_is_mp())
        wifi_resume_powersave();

    return HCI_SUCCESS;
}

uint8_t hci_platform_init_done(void)
{
#if 0
    uint8_t orignal_hci_phy_efuse[0x10];
    hci_uart_out();
    return HCI_FAIL;
#endif

    if (rltk_wlan_is_mp())
    {
#ifdef FT_MODE
        uint8_t TestItem;
        static bool write_efuse_ok = false;

        HCI_DBG("\r\n================= original ========================");
        for (int i = 0; i < 16; i++)
        {
            EFUSE_PMAP_READ8(0, 0x120 + i, orignal_hci_phy_efuse + i, L25EOUTVOLTAGE);
            HCI_DBG("\r\n original physical efuse 0x%x =0x%02x", 0x120 + i, orignal_hci_phy_efuse[i]);
            if (orignal_hci_phy_efuse[i] != 0xff)
                write_efuse_ok = true;
        }

        HCI_DBG("\r\n================= will write physical efuse ========================");
        for (int i = 0; i < 0x10; i++)
        {
            HCI_DBG("\r\n 0x%x =0x%02x", 0x120 + i, hci_phy_efuse[i]);
            // EFUSE_PMAP_WRITE8(0, 0x120 + i, hci_phy_efuse[i], L25EOUTVOLTAGE);
        }

        /* TODO: GNT_BT TO WIFI */
        bt_change_gnt_wifi_only();
        HCI_DBG("EFUSE_SW_MP_MODE: UPPERSTACK NOT UP\r\nGNT_BT %x...", HAL_READ32(0x40080000, 0x0764));
        while (1)
        {
            if (GPIO_ReadDataBit(_PA_5))
            {
                osif_delay(2);
                if (GPIO_ReadDataBit(_PA_5)) /* still input high */
                {
                    GPIO_WriteBit(_PA_13, 0);
                    TestItem = 0;
                    TestItem = GPIO_ReadDataBit(_PB_7);          /* bit0 */
                    TestItem |= (GPIO_ReadDataBit(_PA_17) << 1); /* bit1 */
                    TestItem |= (GPIO_ReadDataBit(_PA_19) << 2); /* bit2 */
                    TestItem |= (GPIO_ReadDataBit(_PA_20) << 3); /* bit3 */
                    TestItem |= (GPIO_ReadDataBit(_PA_6) << 4);  /* bit4 */
                    HCI_DBG("TestItem is %d!!!\n", TestItem);
                    switch (TestItem)
                    {
                    case 13:
                        if (write_efuse_ok == false)
                        {
                            bt_write_phy_efuse_value();
                            write_efuse_ok = true;
                        }
                        else
                        {
                            HCI_DBG("ERROR: phy_efuse has been write, please check the phy_efuse value");
                            /* TODO: tell the GPIO */
                        }
                        break;
                    default:
                        break;
                    }

                    HCI_DBG("trx done");
                    GPIO_WriteBit(_PA_13, 1);
                }
            }
            osif_delay(15);
        }
#endif
        return HCI_FAIL;
    }

    if (!CHECK_SW(EFUSE_SW_UPPERSTACK_SWITCH))
    {
        HCI_DBG("Not Start upperStack, normal test");
        return HCI_FAIL;
    }

    HCI_DBG("Start upperStack\n");
    return HCI_SUCCESS;
}
