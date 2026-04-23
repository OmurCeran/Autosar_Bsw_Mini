#ifdef DEBUG_AUTOSAR

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    /* Task stack taştı */
    (void)xTask;
    
    __disable_irq();
    
    /* Polling UART ile task adını bas */
    HAL_UART_Transmit(&huart2, 
                      (uint8_t*)"\r\n!!! STACK OVERFLOW in task: ", 
                      29, 
                      HAL_MAX_DELAY);
    HAL_UART_Transmit(&huart2, 
                      (uint8_t*)pcTaskName, 
                      strlen(pcTaskName), 
                      HAL_MAX_DELAY);
    HAL_UART_Transmit(&huart2, 
                      (uint8_t*)" !!!\r\n", 
                      6, 
                      HAL_MAX_DELAY);
    
    while(1) { }
}

/**
 * HardFault Handler with full stack frame dump.
 *
 * Approach: On hard fault, the Cortex-M4 pushes 8 words onto the stack:
 *   SP+0x00 : R0
 *   SP+0x04 : R1
 *   SP+0x08 : R2
 *   SP+0x0C : R3
 *   SP+0x10 : R12
 *   SP+0x14 : LR  (return address of the function that faulted)
 *   SP+0x18 : PC  (address of the instruction that faulted) ← KEY
 *   SP+0x1C : xPSR (status register)
 *
 * We extract PC and LR, then print them via polling UART
 * (DMA printf cannot work during fault — IRQs may be disabled).
 *
 * Then we use addr2line to map PC to source file:line.
 */

/* Add these at the top of stm32f4xx_it.c if not already there */


extern UART_HandleTypeDef huart2;

/* Polling-based print — safe during fault (no RTOS, no DMA) */
static void HardFault_Print(const char *msg)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
}

static void HardFault_PrintHex(const char *label, uint32_t value)
{
    char buf[64];
    /* No snprintf — manual hex format to avoid stack usage */
    const char hex[] = "0123456789ABCDEF";
    int i = 0;

    /* Copy label */
    while (*label && i < 40) { buf[i++] = *label++; }

    /* "= 0x" */
    buf[i++] = '=';
    buf[i++] = ' ';
    buf[i++] = '0';
    buf[i++] = 'x';

    /* 8 hex digits */
    for (int shift = 28; shift >= 0; shift -= 4)
    {
        buf[i++] = hex[(value >> shift) & 0xF];
    }

    buf[i++] = '\r';
    buf[i++] = '\n';
    buf[i] = 0;

    HAL_UART_Transmit(&huart2, (uint8_t*)buf, i, HAL_MAX_DELAY);
}

/* Called from HardFault_Handler assembly wrapper with stack pointer */
void HardFault_Report(uint32_t *stackFrame)
{
    uint32_t r0   = stackFrame[0];
    uint32_t r1   = stackFrame[1];
    uint32_t r2   = stackFrame[2];
    uint32_t r3   = stackFrame[3];
    uint32_t r12  = stackFrame[4];
    uint32_t lr   = stackFrame[5];   /* SP + 0x14 */
    uint32_t pc   = stackFrame[6];   /* SP + 0x18  ← instruction that crashed */
    uint32_t psr  = stackFrame[7];   /* SP + 0x1C */

    /* Read fault status registers */
    uint32_t cfsr  = *(volatile uint32_t*)0xE000ED28;   /* Configurable Fault Status */
    uint32_t hfsr  = *(volatile uint32_t*)0xE000ED2C;   /* HardFault Status */
    uint32_t mmfar = *(volatile uint32_t*)0xE000ED34;   /* MemManage Fault Address */
    uint32_t bfar  = *(volatile uint32_t*)0xE000ED38;   /* BusFault Address */

    HardFault_Print("\r\n");
    HardFault_Print("================================================\r\n");
    HardFault_Print("           !!! HARD FAULT !!!                   \r\n");
    HardFault_Print("================================================\r\n");

    HardFault_PrintHex("R0    ", r0);
    HardFault_PrintHex("R1    ", r1);
    HardFault_PrintHex("R2    ", r2);
    HardFault_PrintHex("R3    ", r3);
    HardFault_PrintHex("R12   ", r12);
    HardFault_PrintHex("LR    ", lr);
    HardFault_PrintHex("PC    ", pc);
    HardFault_PrintHex("PSR   ", psr);
    HardFault_Print("----------------------------------------\r\n");
    HardFault_PrintHex("CFSR  ", cfsr);
    HardFault_PrintHex("HFSR  ", hfsr);
    HardFault_PrintHex("MMFAR ", mmfar);
    HardFault_PrintHex("BFAR  ", bfar);
    HardFault_Print("================================================\r\n");
    HardFault_Print(" Use: arm-none-eabi-addr2line -e <binary>.elf -f <PC>\r\n");
    HardFault_Print("================================================\r\n");

    while (1) { /* halt */ }
}

/* Replace the existing empty HardFault_Handler with this */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile
    (
        " tst   lr, #4                \n"  /* determine which stack was active */
        " ite   eq                    \n"
        " mrseq r0, msp               \n"  /* main stack */
        " mrsne r0, psp               \n"  /* process stack */
        " b     HardFault_Report      \n"  /* pass SP in R0 */
    );
}
#endif /* DEBUG_AUTOSAR */