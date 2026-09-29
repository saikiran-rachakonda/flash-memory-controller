#ifndef HOST_PROTOCOL_ENGINE_H
#define HOST_PROTOCOL_ENGINE_H

#include <systemc.h>
#include <vector>
#include <string>
#include <cstdint>
#include "bus.h"

// Base address & register offsets for Host Protocol Engine (IP-08)
constexpr uint32_t PE_BASE             = 0x20000700u;
constexpr uint32_t PE_SIZE             = 256u;

constexpr uint32_t PE_CTRL_OFFSET       = 0x00u;
constexpr uint32_t PE_STATUS_OFFSET     = 0x04u;
constexpr uint32_t PE_FRAMES_OK_OFFSET  = 0x08u;
constexpr uint32_t PE_FRAMES_ERR_OFFSET = 0x0Cu;
constexpr uint32_t PE_LAST_ERROR_OFFSET = 0x10u;
constexpr uint32_t PE_TIMEOUT_NS_OFFSET = 0x14u;
constexpr uint32_t PE_IRQ_ENABLE_OFFSET = 0x18u;

// External peripheral base addresses
constexpr uint32_t UART_BASE           = 0x20000600u;
constexpr uint32_t HI_BASE             = 0x20000000u;
constexpr uint32_t RAM_BASE            = 0x30000000u;

// External peripheral register offsets
constexpr uint32_t UART_RXDATA_OFFSET  = 0x00u;
constexpr uint32_t UART_TXDATA_OFFSET  = 0x04u;
constexpr uint32_t UART_CHAR_NS_OFFSET = 0x10u;
constexpr uint32_t UART_RX_COUNT_OFFSET = 0x18u;
constexpr uint32_t UART_TX_COUNT_OFFSET = 0x1Cu;

constexpr uint32_t HI_CMD_OFFSET        = 0x00u;
constexpr uint32_t HI_FLASH_ADDR_OFFSET = 0x04u;
constexpr uint32_t HI_RAM_ADDR_OFFSET   = 0x08u;
constexpr uint32_t HI_LEN_OFFSET        = 0x0Cu;
constexpr uint32_t HI_STATUS_OFFSET     = 0x10u;

// Error codes defined in IP-08 Specification Section 5
constexpr uint8_t ERR_OK             = 0x00u;
constexpr uint8_t ERR_FRAME_BAD_CSUM = 0x11u;
constexpr uint8_t ERR_FRAME_BAD_EOF  = 0x12u;
constexpr uint8_t ERR_FRAME_BAD_OP   = 0x13u;
constexpr uint8_t ERR_FRAME_TIMEOUT  = 0x14u;
constexpr uint8_t ERR_FRAME_TOO_LONG = 0x15u;

class HostProtocolEngine : public sc_core::sc_module, public IBusSlave
{
public:
    enum State { IDLE = 0, HDR = 1, PAYLOAD = 2, CSUM = 3, EOFRAME = 4, EXEC = 5 };

    sc_core::sc_in<bool>  uart_rx_irq_in;
    sc_core::sc_out<bool> pe_done_out;
    sc_core::sc_out<bool> pe_error_out;

    HostProtocolEngine(sc_core::sc_module_name name, Bus &bus);
    SC_HAS_PROCESS(HostProtocolEngine);

    void bus_access(uint32_t addr, uint32_t &data, bool is_write) override;

private:
    void engine_thread();
    bool get_byte(unsigned char &b);
    void put_byte(uint8_t b);
    void respond(uint8_t status, const std::vector<uint8_t> &payload);

    Bus &m_bus;
    State m_state = IDLE;
    uint32_t m_ctrl = 0;               // Bit 0: ENABLE, Bit 2: ABORT
    uint32_t m_timeout_ns = 10000000;  // 10 ms inter-byte timeout
    uint32_t m_frames_ok = 0;
    uint32_t m_frames_err = 0;
    uint8_t  m_last_error = 0;
    uint32_t m_irq_enable = 0;         // Bit 0: DONE_IRQ_EN, Bit 1: ERROR_IRQ_EN

    std::vector<uint8_t> m_hdr;
    std::vector<uint8_t> m_payload;
    uint8_t m_running_csum = 0;
    bool m_owns_hi = false;
};

#endif // HOST_PROTOCOL_ENGINE_H
