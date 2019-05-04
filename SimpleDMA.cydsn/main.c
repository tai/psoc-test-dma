/* ========================================
 *
 * Copyright YOUR COMPANY, THE YEAR
 * All Rights Reserved
 * UNPUBLISHED, LICENSED SOFTWARE.
 *
 * CONFIDENTIAL AND PROPRIETARY INFORMATION
 * WHICH IS THE PROPERTY OF your company.
 *
 * ========================================
*/
#include "project.h"

// Transfer methods to choose from
#define DMA_NONE 0
#define DMA_WITH_DRQ_TYPE1 1
#define DMA_WITH_DRQ_TYPE2 2
#define DMA_WITH_DRQ_TYPE3 3
#define DMA_WITH_ISR_NRQ   4
#define DMA_WITH_ISR_READY 5

// Choose which method to test
#define DMA_METHOD DMA_WITH_DRQ_TYPE1

//////////////////////////////////////////////////////////////////////

enum { BLINK_NONE, BLINK_SLOW, BLINK_NORMAL, BLINK_FAST, BLINK_VERYFAST };
volatile int blink_speed = BLINK_NORMAL;

void update_blink(void) {
    static int blink_state = BLINK_NONE;

    if (blink_state != blink_speed) {
        blink_state = blink_speed;

        switch (blink_speed) {
        case BLINK_SLOW:
            PWM_1_WritePeriod(255);
            PWM_1_WriteCompare(128);
            break;
        case BLINK_FAST:
            PWM_1_WritePeriod(63);
            PWM_1_WriteCompare(32);
            break;
        case BLINK_VERYFAST:
            PWM_1_WritePeriod(31);
            PWM_1_WriteCompare(16);
            break;
        default:
            PWM_1_WritePeriod(127);
            PWM_1_WriteCompare(64);
        }
    }
}

//////////////////////////////////////////////////////////////////////

uint8 ch;
uint8 td[1];

char buffer[26] = {0};
volatile static uint8 nr_complete;
volatile static int err;

void fill_buffer(void) {
    uint8 i;
    for (i = 0; i < sizeof(buffer); i++) {
        buffer[i] = 'a' + i;
    }
}

CY_ISR(on_READY) {
    ISR_READY_ClearPending();
    blink_speed = BLINK_FAST;

#if DMA_METHOD == DMA_WITH_ISR_NRQ
    nr_complete += 1;    
    if (nr_complete >= sizeof(buffer)) {
        nr_complete = 0;
    }

    //
    // Poll device to see if it's ready for further DMA transfer.
    //
    // FIFO of BitBlit could be still full, especially right after DMAC has completed prior transfer.
    // However, to handle this case, busy-polling of BitBlit Status register is needed, which just isn't good.
    // It's much easier to just route READY flag to trigger interrupt, so ISR can just trigger next DMA request.
    //
    while (! (BitBlit_StatusReg_Read() & 0x1));
    CyDelay(1); // didn't work without extra delay, but definitely a BAD THING in ISR!
    
    CyDmaTdSetAddress(td[0], LO16((uint32)buffer + nr_complete), LO16((uint32)BitBlit_Datapath_F0_PTR));
    CyDmaChSetRequest(ch, CPU_REQ);
#endif

#if DMA_METHOD == DMA_WITH_ISR_READY      
    if (nr_complete >= sizeof(buffer)) {
        nr_complete = 0;
    }
    CyDmaTdSetAddress(td[0], LO16((uint32)buffer + nr_complete), LO16((uint32)BitBlit_Datapath_F0_PTR));
    CyDmaChSetRequest(ch, CPU_REQ);
    nr_complete += 1;
#endif
}

void testDMA(void) {
    int rc;

    // Blink LED - update rate later to show status
    PWM_1_Start();

#if DMA_METHOD == DMA_WITH_ISR_NRQ || DMA_METHOD == DMA_WITH_ISR_READY
    // Install ISR to control DMA
    ISR_READY_StartEx(on_READY);
#endif

    // Allocate Transfer Description
    td[0] = CyDmaTdAllocate();
    if (td[0] == CY_DMA_INVALID_TD) goto error;

#if DMA_METHOD == DMA_WITH_DRQ_TYPE1
    /*
     * Results to a-za-za-z... output on TX.
     * However, NRQ does not trigger, probably because this is a never-ending DMA with td[0]->td[0] chain.
     */
    ch = DMA_1_DmaInitialize(1, 1, HI16(CYDEV_SRAM_BASE), HI16(CYDEV_PERIPH_BASE));
    if (ch == DMA_INVALID_CHANNEL) goto error;
    rc = CyDmaTdSetConfiguration(td[0], sizeof(buffer), td[0], TD_INC_SRC_ADR|DMA_1__TD_TERMOUT_EN);
    if (rc != CYRET_SUCCESS) goto error;
#endif

#if DMA_METHOD == DMA_WITH_DRQ_TYPE2
    /* 
     * Results to a-za-za-z... output on TX.
     * It loops even with td[0]->DMA_END_CHAIN_TD chain, because drq is level-triggered by READY output.
     * However, NRQ does not trigger. It's bit unclear when I can expect NRQ to trigger...
     */
    ch = DMA_1_DmaInitialize(
        1 /* bytes to send in a burst - use 1, as READY signal only shows FIFO has at least 1-byte slot */,
        1 /* 0: second and trailing bursts will trigger automatically, 1: each burst needs to be triggered in some way */,
        HI16(CYDEV_SRAM_BASE), HI16(CYDEV_PERIPH_BASE));
    if (ch == DMA_INVALID_CHANNEL) goto error;
    rc = CyDmaTdSetConfiguration(td[0], sizeof(buffer), DMA_END_CHAIN_TD, TD_INC_SRC_ADR|DMA_1__TD_TERMOUT_EN);
    if (rc != CYRET_SUCCESS) goto error;
#endif

#if DMA_METHOD == DMA_WITH_DRQ_TYPE3
    /*
     * Results to one-shot output of a-z on TX.
     * However, NRQ still does not trigger. It may be because DMA is disabled by DMA_DISABLE_TD.
     */
    ch = DMA_1_DmaInitialize(1, 1, HI16(CYDEV_SRAM_BASE), HI16(CYDEV_PERIPH_BASE));
    if (ch == DMA_INVALID_CHANNEL) goto error;
    rc = CyDmaTdSetConfiguration(td[0], sizeof(buffer), DMA_DISABLE_TD, TD_INC_SRC_ADR|DMA_1__TD_TERMOUT_EN);
    if (rc != CYRET_SUCCESS) goto error;
#endif

#if DMA_METHOD == DMA_WITH_ISR_NRQ || DMA_METHOD == DMA_WITH_ISR_READY
    /*
     * Results to a-za-za-z... on TX.
     * NRQ triggers.
     * However, not really a good option for 1-byte DMA, as every DMA triggers interrupt to a CPU.
     */
    ch = DMA_1_DmaInitialize(
        1 /* bytes to send in a burst */,
        1 /* 0: second and trailing bursts will trigger automatically, 1: each burst needs to be triggered in some way */,
        HI16(CYDEV_SRAM_BASE), HI16(CYDEV_PERIPH_BASE));
    if (ch == DMA_INVALID_CHANNEL) goto error;
    rc = CyDmaTdSetConfiguration(td[0], 1, DMA_END_CHAIN_TD, DMA_1__TD_TERMOUT_EN);
    if (rc != CYRET_SUCCESS) goto error;
#endif
    
    rc = CyDmaTdSetAddress(td[0], LO16((uint32)buffer), LO16((uint32)BitBlit_Datapath_F0_PTR));
    if (rc != CYRET_SUCCESS) goto error;

    rc = CyDmaChSetInitialTd(ch, td[0]);
    if (rc != CYRET_SUCCESS) goto error;
    
    rc = CyDmaChEnable(ch, 1 /* preserve TD data - MUST be 1 to chain back to the same TD in CyDmaTdSetConfiguration */);
    if (rc != CYRET_SUCCESS) goto error;
    
    // Enable device (and also DMAC drq connected to READY pin)
    BitBlit_CtrlReg_Write(0x1);
    
#if DMA_METHOD == DMA_WITH_ISR_NRQ
    // Start initial DMA in software-driven manner
    CyDmaChSetRequest(ch, CPU_REQ);
#endif

    for (;;) {
        update_blink();
    }

error:
    PWM_1_Stop();
    err = CyDmacError();
    for (;;) {
        CyDelay(100);
    }
}

void testUDB(void) {
    // Blink LED
    PWM_1_Start();

    BitBlit_CtrlReg_Write(0x1);

    for (;;) {
        int c;
        for (c = 'a'; c <= 'z'; c++) {
            while (! (BitBlit_StatusReg_Read() & 0x2));
            BitBlit_Datapath_F0_REG = c;
        }
        while (BitBlit_StatusReg_Read() & 0x1);
        CyDelay(10);
        update_blink();
    }
}

int main(void) {
    CyGlobalIntEnable; /* Enable global interrupts. */

    fill_buffer();

#if DMA_METHOD == DMA_NONE
    testUDB();
#else
    testDMA();
#endif
}

/* [] END OF FILE */
