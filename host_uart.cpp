#include "host_uart.h"
#include <string.h>

void host_uart_init(HostUart *uart, uint32_t baudrate) {
    if (!uart) return;

    uart->rx_head = 0;
    uart->rx_tail = 0;
    uart->tx_head = 0;
    uart->tx_tail = 0;
    uart->baudrate = baudrate;
    uart->is_initialized = true;

    // TODO: Register hardware level UART registers/clocks here if applicable
}

bool host_uart_write_byte(HostUart *uart, uint8_t data) {
    if (!uart || !uart->is_initialized) return false;

    uint16_t next_head = (uart->tx_head + 1) % HOST_UART_TX_BUFFER_SIZE;
   
    // Buffer full check
    if (next_head == uart->tx_tail) {
        return false;
    }

    uart->tx_buf[uart->tx_head] = data;
    uart->tx_head = next_head;

    // Trigger hardware transmission interrupt if idle
    // hardware_enable_tx_interrupt();

    return true;
}

size_t host_uart_write(HostUart *uart, const uint8_t *data, size_t length) {
    size_t bytes_written = 0;
    for (size_t i = 0; i < length; i++) {
        if (!host_uart_write_byte(uart, data[i])) {
            break; // Stop if TX buffer is full
        }
        bytes_written++;
    }
    return bytes_written;
}

bool host_uart_read_byte(HostUart *uart, uint8_t *data) {
    if (!uart || !uart->is_initialized || (uart->rx_head == uart->rx_tail)) {
        return false; // Buffer empty
    }

    *data = uart->rx_buf[uart->rx_tail];
    uart->rx_tail = (uart->rx_tail + 1) % HOST_UART_RX_BUFFER_SIZE;
    return true;
}

size_t host_uart_read(HostUart *uart, uint8_t *buffer, size_t max_length) {
    size_t bytes_read = 0;
    while (bytes_read < max_length && host_uart_read_byte(uart, &buffer[bytes_read])) {
        bytes_read++;
    }
    return bytes_read;
}

// Called by the MCU RX Interrupt Service Routine
void host_uart_rx_isr_handler(HostUart *uart, uint8_t incoming_byte) {
    uint16_t next_head = (uart->rx_head + 1) % HOST_UART_RX_BUFFER_SIZE;
   
    // Overwrite safety: drop byte if RX buffer is full
    if (next_head != uart->rx_tail) {
        uart->rx_buf[uart->rx_head] = incoming_byte;
        uart->rx_head = next_head;
    }
}

// Called by the MCU TX Ready Interrupt Service Routine
void host_uart_tx_isr_handler(HostUart *uart) {
    if (uart->tx_head != uart->tx_tail) {
        uint8_t byte_to_send = uart->tx_buf[uart->tx_tail];
        uart->tx_tail = (uart->tx_tail + 1) % HOST_UART_TX_BUFFER_SIZE;
       
        // Transmit byte to hardware UART register
        // HARDWARE_UART_DATA_REG = byte_to_send;
    } else {
        // Buffer empty, disable hardware TX interrupt
        // hardware_disable_tx_interrupt();
    }
}
