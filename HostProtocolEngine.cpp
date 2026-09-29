/*
 * Host Protocol Engine FSM Specification and State Transitions:
 *
 * FSM States (State Enum):
 * 0. IDLE    (0): Engine is waiting for frame start byte (0xA5) from Host UART.
 * 1. HDR     (1): Receiving and parsing 7-byte header (OPCODE, ADDR[4], LEN[2]).
 * 2. PAYLOAD (2): Receiving LEN bytes of payload for WRITE command.
 * 3. CSUM    (3): Receiving and verifying frame CHECKSUM byte.
 * 4. EOFRAME (4): Receiving and verifying EOF marker byte (0x5A).
 * 5. EXEC    (5): Executing command (WRITE/READ/ERASE via Host Interface, or STATUS).
 *
 * State Transitions:
 *
 * State: IDLE
 *  - Event: get_byte() receives 0xA5 (SOF)
 *    -> Action: Reset checksum accumulator, clear header/payload buffers. Transition to HDR.
 *  - Event: get_byte() receives byte != 0xA5
 *    -> Action: Silently discard byte. Transition to IDLE.
 *  - Event: ENABLE bit == 0 or ABORT bit written
 *    -> Action: Remain in IDLE.
 *
 * State: HDR
 *  - Event: Successfully received 7 header bytes (OPCODE, ADDR[4], LEN[2])
 *    - Check 1: OPCODE is not 1, 2, 3, or 4 -> FRAME_BAD_OP (0x13). Send error reply, transition to IDLE.
 *    - Check 2: LEN > 4096 or (LEN % 4 != 0) -> FRAME_TOO_LONG (0x15). Send error reply, transition to IDLE.
 *    - Check 3: OPCODE == 1 (WRITE) and LEN > 0 -> Transition to PAYLOAD.
 *    - Check 4: Otherwise (READ/ERASE/STATUS or WRITE with LEN=0) -> Transition to CSUM.
 *  - Event: get_byte() timeout or ABORT
 *    -> Action: If ABORT, self-clear bit and return to IDLE. If timeout, record ERR_FRAME_TIMEOUT (0x14),
 *               send error reply, transition to IDLE.
 *
 * State: PAYLOAD
 *  - Event: Successfully received LEN payload bytes
 *    -> Action: Accumulate running checksum. Transition to CSUM.
 *  - Event: get_byte() timeout or ABORT
 *    -> Action: If ABORT, return to IDLE. If timeout, send ERR_FRAME_TIMEOUT reply, transition to IDLE.
 *
 * State: CSUM
 *  - Event: get_byte() receives checksum byte
 *    - Check: Received byte matches running checksum
 *      -> Yes: Transition to EOFRAME.
 *      -> No: FRAME_BAD_CSUM (0x11). Send error reply, transition to IDLE.
 *  - Event: get_byte() timeout or ABORT
 *    -> Action: If ABORT, return to IDLE. If timeout, send ERR_FRAME_TIMEOUT reply, transition to IDLE.
 *
 * State: EOFRAME
 *  - Event: get_byte() receives EOF byte
 *    - Check: Received byte == 0x5A
 *      -> Yes: Transition to EXEC.
 *      -> No: FRAME_BAD_EOF (0x12). Send error reply, transition to IDLE.
 *  - Event: get_byte() timeout or ABORT
 *    -> Action: If ABORT, return to IDLE. If timeout, send ERR_FRAME_TIMEOUT reply, transition to IDLE.
 *
 * State: EXEC
 *  - Event: Execution completes
 *    - OPCODE 4 (STATUS): Read HI_STATUS, send reply (0x00) with 4-byte payload. Increment m_frames_ok, transition to IDLE.
 *    - OPCODE 1 (WRITE): Stage payload to Data RAM, launch HI WRITE, poll HI_STATUS.
 *      -> Success: Send reply (0x00), transition to IDLE.
 *      -> Error: Pass-through HI error code (0x01..0x05), send error reply, transition to IDLE.
 *    - OPCODE 2 (READ): Launch HI READ, poll HI_STATUS.
 *      -> Success: Read payload from Data RAM, send reply (0x00) with payload, transition to IDLE.
 *      -> Error: Pass-through HI error code (0x01..0x05), send error reply, transition to IDLE.
 *    - OPCODE 3 (ERASE): Launch HI ERASE, poll HI_STATUS.
 *      -> Success: Send reply (0x00), transition to IDLE.
 *      -> Error: Pass-through HI error code (0x01..0x05), send error reply, transition to IDLE.
 */

#include "host_protocol_engine.h"

HostProtocolEngine::HostProtocolEngine(sc_core::sc_module_name name, Bus &bus)
    : sc_core::sc_module(name),
      m_bus(bus)
{
    // Register slave address window on interconnect
    m_bus.register_slave(std::string(name), PE_BASE, PE_SIZE, this);

    // Register primary FSM thread
    SC_THREAD(engine_thread);

    // Drive interrupt output pins false initially
    pe_done_out.initialize(false);
    pe_error_out.initialize(false);
}

void HostProtocolEngine::bus_access(uint32_t addr, uint32_t &data, bool is_write)
{
    switch (addr) {
    case PE_CTRL_OFFSET:
        if (is_write) {
            // [0] ENABLE
            if (data & 0x01u) {
                m_ctrl |= 0x01u;
            } else {
                m_ctrl &= ~0x01u;
            }

            // [1] STATS_RESET (Self-clearing)
            if (data & 0x02u) {
                m_frames_ok = 0;
                m_frames_err = 0;
                m_last_error = 0;
            }

            // [2] ABORT (Self-clearing)
            if (data & 0x04u) {
                m_ctrl |= 0x04u;
            }
        } else {
            data = m_ctrl & 0x01u; // Only ENABLE bit is persistent on reads
        }
        break;

    case PE_STATUS_OFFSET:
        if (!is_write) {
            uint32_t val = 0;
            if (m_state == IDLE) val |= 0x01u;              // [0] IDLE
            val |= (static_cast<uint32_t>(m_state) & 0x07u) << 1; // [3:1] STATE
            if (m_owns_hi) val |= (1u << 4);                // [4] OWNS_HI
            data = val;
        }
        break;

    case PE_FRAMES_OK_OFFSET:
        if (!is_write) data = m_frames_ok;
        break;

    case PE_FRAMES_ERR_OFFSET:
        if (!is_write) data = m_frames_err;
        break;

    case PE_LAST_ERROR_OFFSET:
        if (!is_write) data = m_last_error;
        break;

    case PE_TIMEOUT_NS_OFFSET:
        if (is_write) {
            m_timeout_ns = data;
        } else {
            data = m_timeout_ns;
        }
        break;

    case PE_IRQ_ENABLE_OFFSET:
        if (is_write) {
            m_irq_enable = data & 0x03u;
        } else {
            data = m_irq_enable;
        }
        break;

    default:
        if (!is_write) data = 0;
        break;
    }
}

bool HostProtocolEngine::get_byte(unsigned char &b)
{
    sc_time deadline = sc_time_stamp() + sc_time(m_timeout_ns, SC_NS);

    while (true) {
        // Check for ABORT signal
        if (m_ctrl & 0x04u) {
            return false;
        }

        // Re-check UART_RX_COUNT before sleeping (Lost-wakeup trap fix, IP-08 §4.5)
        uint32_t n = 0;
        m_bus.read(UART_BASE + UART_RX_COUNT_OFFSET, n);
        if (n > 0) {
            uint32_t v = 0;
            m_bus.read(UART_BASE + UART_RXDATA_OFFSET, v); // Pop FIFO
            b = static_cast<unsigned char>(v & 0xFFu);
            return true;
        }

        if (sc_time_stamp() >= deadline) {
            return false;
        }

        // Wait on time deadline or UART RX pulse
        wait(deadline - sc_time_stamp(), uart_rx_irq_in.posedge_event());
    }
}

void HostProtocolEngine::put_byte(uint8_t b)
{
    uint32_t n = 0;
    uint32_t cns = 0;

    // Read character transmit duration in nanoseconds
    m_bus.read(UART_BASE + UART_CHAR_NS_OFFSET, cns);
    if (cns == 0) cns = 100; // Safeguard non-zero delay

    // TX Flow Control (IP-08 §4.6)
    while (true) {
        m_bus.read(UART_BASE + UART_TX_COUNT_OFFSET, n);
        if (n < 16) break; // Room available in 16-byte FIFO
        wait(sc_time(static_cast<double>(cns), SC_NS));
    }

    m_bus.write(UART_BASE + UART_TXDATA_OFFSET, b);
}

void HostProtocolEngine::respond(uint8_t status, const std::vector<uint8_t> &payload)
{
    uint16_t len = static_cast<uint16_t>(payload.size());

    // Construct response frame
    put_byte(0xA5); // SOF
    put_byte(status);
    put_byte(static_cast<uint8_t>(len & 0xFFu));
    put_byte(static_cast<uint8_t>((len >> 8) & 0xFFu));

    uint8_t csum = status ^ static_cast<uint8_t>(len & 0xFFu) ^ static_cast<uint8_t>((len >> 8) & 0xFFu);

    for (uint8_t byte : payload) {
        put_byte(byte);
        csum ^= byte;
    }

    put_byte(csum);
    put_byte(0x5A); // EOF

    // Handle frame counters & interrupt pulses
    if (status == ERR_OK) {
        m_frames_ok++;
        if (m_irq_enable & 0x01u) { // DONE_IRQ_EN
            pe_done_out.write(true);
            wait(SC_ZERO_TIME);
            pe_done_out.write(false);
        }
    } else {
        m_last_error = status;
        m_frames_err++;
        if (m_irq_enable & 0x02u) { // ERROR_IRQ_EN
            pe_error_out.write(true);
            wait(SC_ZERO_TIME);
            pe_error_out.write(false);
        }
    }
}

void HostProtocolEngine::engine_thread()
{
    pe_done_out.write(false);
    pe_error_out.write(false);

    while (true) {
        m_state = IDLE;
        m_owns_hi = false;

        // Check if Host Protocol Engine is enabled
        if (!(m_ctrl & 0x01u)) {
            wait(10, SC_NS);
            continue;
        }

        // Wait for SOF byte (0xA5)
        unsigned char b = 0;
        if (!get_byte(b)) {
            if (m_ctrl & 0x04u) m_ctrl &= ~0x04u; // Self-clear ABORT
            continue;
        }

        // Requirement 5: Bytes before 0xA5 are discarded silently with NO reply
        if (b != 0xA5) {
            continue;
        }

        // Transition to HDR State
        m_state = HDR;
        m_running_csum = 0;
        m_hdr.clear();
        m_payload.clear();

        bool frame_aborted = false;
        bool frame_timed_out = false;

        // Read remaining 7 header bytes (OPCODE, ADDR[4], LEN[2])
        for (int i = 0; i < 7; ++i) {
            if (!get_byte(b)) {
                if (m_ctrl & 0x04u) frame_aborted = true;
                else frame_timed_out = true;
                break;
            }
            m_hdr.push_back(b);
            m_running_csum ^= b;
        }

        if (frame_aborted) {
            m_ctrl &= ~0x04u;
            continue;
        }
        if (frame_timed_out) {
            respond(ERR_FRAME_TIMEOUT, {});
            continue;
        }

        uint8_t opcode = m_hdr[0];
        uint32_t addr = m_hdr[1] | (m_hdr[2] << 8) | (m_hdr[3] << 16) | (m_hdr[4] << 24);
        uint16_t len  = m_hdr[5] | (m_hdr[6] << 8);

        // Validate OPCODE
        if (opcode < 1 || opcode > 4) {
            respond(ERR_FRAME_BAD_OP, {});
            continue;
        }

        // Validate LEN limits
        if (len > 4096 || (len % 4 != 0)) {
            respond(ERR_FRAME_TOO_LONG, {});
            continue;
        }

        // Transition to PAYLOAD State (WRITE command only)
        if (opcode == 1 && len > 0) {
            m_state = PAYLOAD;
            for (uint16_t i = 0; i < len; ++i) {
                if (!get_byte(b)) {
                    if (m_ctrl & 0x04u) frame_aborted = true;
                    else frame_timed_out = true;
                    break;
                }
                m_payload.push_back(b);
                m_running_csum ^= b;
            }

            if (frame_aborted) {
                m_ctrl &= ~0x04u;
                continue;
            }
            if (frame_timed_out) {
                respond(ERR_FRAME_TIMEOUT, {});
                continue;
            }
        }

        // Transition to CSUM State
        m_state = CSUM;
        unsigned char rx_csum = 0;
        if (!get_byte(rx_csum)) {
            if (m_ctrl & 0x04u) m_ctrl &= ~0x04u;
            else respond(ERR_FRAME_TIMEOUT, {});
            continue;
        }

        if (rx_csum != m_running_csum) {
            respond(ERR_FRAME_BAD_CSUM, {});
            continue;
        }

        // Transition to EOFRAME State
        m_state = EOFRAME;
        unsigned char rx_eof = 0;
        if (!get_byte(rx_eof)) {
            if (m_ctrl & 0x04u) m_ctrl &= ~0x04u;
            else respond(ERR_FRAME_TIMEOUT, {});
            continue;
        }

        if (rx_eof != 0x5A) {
            respond(ERR_FRAME_BAD_EOF, {});
            continue;
        }

        // Transition to EXEC State
        m_state = EXEC;

        // Opcode 4: STATUS (Host-side debug hook, does not touch Host Interface)
        if (opcode == 4) {
            uint32_t hi_status = 0;
            m_bus.read(HI_BASE + HI_STATUS_OFFSET, hi_status);

            std::vector<uint8_t> status_payload(4);
            status_payload[0] = static_cast<uint8_t>(hi_status & 0xFFu);
            status_payload[1] = static_cast<uint8_t>((hi_status >> 8) & 0xFFu);
            status_payload[2] = static_cast<uint8_t>((hi_status >> 16) & 0xFFu);
            status_payload[3] = static_cast<uint8_t>((hi_status >> 24) & 0xFFu);

            respond(ERR_OK, status_payload);
            continue;
        }

        // Opcodes 1, 2, 3 engage Host Interface
        m_owns_hi = true;

        // If WRITE, stage payload words into Data RAM (IP-08 §4.4)
        if (opcode == 1) {
            for (uint32_t i = 0; i < len; i += 4) {
                uint32_t w = 0;
                for (uint32_t b_idx = 0; b_idx < 4; ++b_idx) {
                    if (i + b_idx < len) {
                        w |= static_cast<uint32_t>(m_payload[i + b_idx]) << (8 * b_idx);
                    }
                }
                m_bus.write(RAM_BASE + i, w);
            }
        }

        // Configure Host Interface registers
        m_bus.write(HI_BASE + HI_FLASH_ADDR_OFFSET, addr);
        m_bus.write(HI_BASE + HI_RAM_ADDR_OFFSET, 0);
        m_bus.write(HI_BASE + HI_LEN_OFFSET, len);

        // Launch Host Interface command (Opcode [3:0], START bit [8])
        uint32_t hi_cmd = (opcode & 0x0Fu) | (1u << 8);
        m_bus.write(HI_BASE + HI_CMD_OFFSET, hi_cmd);

        // Poll HI_STATUS until BUSY (bit 0) clears
        uint32_t hi_status = 0;
        while (true) {
            m_bus.read(HI_BASE + HI_STATUS_OFFSET, hi_status);
            if (!(hi_status & 0x01u)) break; // Not busy
            wait(10, SC_NS);
        }

        // Check if Host Interface reported an error (Bit 2)
        if (hi_status & 0x04u) {
            uint8_t hi_err_code = static_cast<uint8_t>((hi_status >> 4) & 0x0Fu);
            m_owns_hi = false;
            respond(hi_err_code, {});
            continue;
        }

        // Command completed successfully
        if (opcode == 2) { // READ command: Unpack Data RAM into response payload
            std::vector<uint8_t> read_payload(len);
            for (uint32_t i = 0; i < len; i += 4) {
                uint32_t w = 0;
                m_bus.read(RAM_BASE + i, w);
                for (uint32_t b_idx = 0; b_idx < 4; ++b_idx) {
                    if (i + b_idx < len) {
                        read_payload[i + b_idx] = static_cast<uint8_t>((w >> (8 * b_idx)) & 0xFFu);
                    }
                }
            }
            m_owns_hi = false;
            respond(ERR_OK, read_payload);
        } else { // WRITE or ERASE command
            m_owns_hi = false;
            respond(ERR_OK, {});
        }
    }
}
