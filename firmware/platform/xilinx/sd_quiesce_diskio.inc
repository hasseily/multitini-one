/* SPDX-License-Identifier: GPL-3.0-only */
/* Inserted into generated xilffs diskio.c by create_vitis_workspace.py.
 * The shared driver owns SdInstance and Stat; no second controller instance. */
#if defined(FILE_SYSTEM_INTERFACE_SD) && defined(XPAR_XSDPS_NUM_INSTANCES)
static u8 MultitiniSdFault[XSDPS_NUM_INSTANCES];

static DRESULT MultitiniSdQuiesce(BYTE pdrv, void *buff)
{
    const u32 active = XSDPS_PSR_INHIBIT_CMD_MASK | XSDPS_PSR_INHIBIT_DAT_MASK |
                       XSDPS_PSR_DAT_ACTIVE_MASK | XSDPS_PSR_WR_ACTIVE_MASK |
                       XSDPS_PSR_RD_ACTIVE_MASK;
    XSdPs *instance;
    UINTPTR base;
    if (buff == NULL || pdrv >= XSDPS_NUM_INSTANCES) return RES_PARERR;
    *(BYTE *)buff = 0U;
    /* Resolve the controller base through the same normal driver path even
     * when the card is absent or initialization has not completed. */
    (void)disk_status(pdrv);
    instance = &SdInstance[pdrv];
    base = instance->Config.BaseAddress != 0U ? instance->Config.BaseAddress : BaseAddress[pdrv];
    if (base == 0U) return RES_NOTRDY;
    if (MultitiniSdFault[pdrv] != 0U ||
        (XSdPs_GetPresentStatusReg(base) & active) != 0U) {
        Stat[pdrv] |= STA_NOINIT;
        MultitiniSdFault[pdrv] = 1U;
        /* XSdPs_Reset uses only Config.BaseAddress and has a bounded one
         * second reset-complete wait. Keep its existing shared instance. */
        instance->Config.BaseAddress = base;
        if (XSdPs_Reset(instance, XSDPS_SWRST_ALL_MASK) != XST_SUCCESS ||
            (XSdPs_ReadReg8(base, XSDPS_SW_RST_OFFSET) & XSDPS_SWRST_ALL_MASK) != 0U ||
            (XSdPs_GetPresentStatusReg(base) & active) != 0U) return RES_ERROR;
        instance->IsBusy = FALSE;
        instance->IsReady = 0U;
        MultitiniSdFault[pdrv] = 0U;
        *(BYTE *)buff = MT_SD_QUIESCE_RESET;
    }
    return RES_OK;
}
#endif
