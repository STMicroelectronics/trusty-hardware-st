#include <dev/uart.h>
#include <platform.h>
#include <stm32mp-regs.h>
#include <reg.h>

#define READ32(addr) (*REG32(addr))
#define WRITE32(val, addr) (READ32(addr) = val)

#define UART_BASE (SOC_REGS_VIRT + USART2_BASE - SOC_REGS_BASE)

#define UART_REG_ISR 0x1c /* Interrupt & status reg. */
#define UART_REG_RDR 0x24 /* Receive data register */
#define UART_REG_TDR 0x28 /* Transmit data register */

/*
 * At 115200 bits/s
 * 1 bit = 1 / 115200 = 8,68us
 * 8 bits = 69,444us
 * 10 bits are needed for worst case (8 bits + 1 start + 1 stop) = 86.806 us
 * Rount it to 90 us.
 */
#define ONE_BYTE_B115200_NS (90 * 1000)
#define UART_DFLT_FIFO_SIZE 64

/*
 * Uart Interrupt & status register bits
 *
 * Bit 5 RXNE: Read data register not empty/RXFIFO not empty
 * Bit 6 TC: Transmission complete
 * Bit 7 TXE/TXFNF: Transmit data register empty/TXFIFO not full
 * Bit 23 TXFE: TXFIFO empty
 */
#define USART_ISR_RXNE_RXFNE (1 << 5)
#define USART_ISR_TC (1 << 6)
#define USART_ISR_TXE_TXFNF (1 << 7)
#define USART_ISR_TXE_TXFE (1 << 23)


void uart_init(void) {
    /*
     * Do nothing, debug uart share with normal world,
     * everything for uart intialization were done in bootloader.
     */
}

void uart_flush_tx(int port) {
    lk_time_ns_t start = current_time_ns();
    lk_time_ns_t timeout = UART_DFLT_FIFO_SIZE * ONE_BYTE_B115200_NS;

    while (!(READ32(UART_BASE + UART_REG_ISR) & USART_ISR_TXE_TXFE))
        if (current_time() - start > timeout)
            return;
}

int uart_putc(int port, char c) {
    if (c == '\n')
        uart_putc(port, '\r');

    lk_time_ns_t start = current_time_ns();
    lk_time_ns_t timeout = ONE_BYTE_B115200_NS;

    while (!(READ32(UART_BASE + UART_REG_ISR) & USART_ISR_TXE_TXFNF))
        if (current_time_ns() - start > timeout)
            return -1;

    WRITE32(c, UART_BASE + UART_REG_TDR);

    return 0;
}

int uart_getc(int port, bool wait) {
    if (wait)
        while (READ32(UART_BASE + UART_REG_ISR) & USART_ISR_RXNE_RXFNE)
            ;
    else if (!(READ32(UART_BASE + UART_REG_ISR) & USART_ISR_RXNE_RXFNE))
        return -1;

    return READ32(UART_BASE + UART_REG_RDR) & 0xff;
}
