#ifndef HOST_UART_H
#define HOST_UART_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define HOST_UART_RX_BUFFER_SIZE 256
#define HOST_UART_TX_BUFFER_SIZE 256

typedef struct {
    uint8_t rx_buf[HOST_UART_RX_BUFFER_SIZE];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
   
    uint8_t tx_buf[HOST_UART_TX_BUFFER_SIZE];
    volatile uint16_t tx_head;
    volatile uint16_t tx_tail;

    uint32_t baudrate;
    bool is_initialized;
} HostUart;

// Core API
void host_uart_init(HostUart *uart, uint32_t baudrate);
bool host_uart_write_byte(HostUart *uart, uint8_t data);
size_t host_uart_write(HostUart *uart, const uint8_t *data, size_t length);
bool host_uart_read_byte(HostUart *uart, uint8_t *data);
size_t host_uart_read(HostUart *uart, uint8_t* buffer, size_t max_length);

// Interrupt/Hardware Handlers
void host_uart_rx_isr_handler(HostUart *uart, uint8_t incoming_byte);
void host_uart_tx_isr_handler(HostUart *uart);

#endif // HOST_UART_H
