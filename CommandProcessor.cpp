#include "CommandProcessor.h"

CommandProcessor::CommandProcessor(sc_core::sc_module_name name, Bus &bus)
    : sc_core::sc_module(name),
      cp_done_out("cp_done_out"),
      cp_error_out("cp_error_out"),
	m_bus(bus),
      m_ctrl(0), m_flash_addr(0), m_ram_addr(0), m_len(0), m_status(0)
{
    // Register structural window onto system bus
    m_bus.register_slave("command_processor", CP_BASE, CP_SIZE, this);

    // Initialize level-sensitive outputs to a defined state at time zero
    cp_done_out.initialize(false);
    cp_error_out.initialize(false);

    // Register Master Process Thread
    SC_THREAD(fsm_thread);
}

void CommandProcessor::bus_access(uint32_t addr, uint32_t &data, bool is_write)
{
    if (addr >= CP_SIZE) {
        if (!is_write) data = 0;
        return;
    }

    if (is_write) {
        switch (addr) {
            case 0x00: // CP_CTRL
                m_ctrl = data & 0x0000000F; // Store OP bits [3:0]
                
                if (data & (1 << 8)) { // START bit strobe asserted
                    // Synchronously prepare local visibility status
                    m_status &= ~0x00000006; // Clear DONE and ERROR flags
                    m_status |= (1 << 0);    // Raise BUSY flag immediately

                    // Awake the background FSM loop to proceed via next delta
                    m_start_sequence_ev.notify(SC_ZERO_TIME);
                }
                break;

            case 0x04: m_flash_addr = data; break;
            case 0x08: m_ram_addr   = data; break;
            case 0x0C: m_len        = data; break;
            
            case 0x10: // CP_STATUS (W1C semantics for bits [1:2])
                m_status &= ~(data & 0x00000006);
                break;

            default:
                break;
        }
    } else {
        // Read handling
        switch (addr) {
            case 0x00: data = m_ctrl;       break; // START bit auto-reads 0
            case 0x04: data = m_flash_addr; break;
            case 0x08: data = m_ram_addr;   break;
            case 0x0C: data = m_len;        break;
            case 0x10: data = m_status;     break;
            default:   data = 0;            break;
        }
    }
}

void CommandProcessor::fsm_thread()
{
    while (true) {
        // Sleep until bus_access catches an orchestrated command execution strobe
        wait(m_start_sequence_ev);

        uint32_t op = m_ctrl & 0xF;
        bool success = true;

        // Sequence Selection 
        if (op == 1) { // WRITE SEQUENCE: RAM -> Flash Staging Stream
            // Step 1: Program DMA Engine
            success &= write_peripheral(DMA_BASE + 0x0C, RAM_BASE + m_ram_addr); // DMA_SRC_ADDR
            success &= write_peripheral(DMA_BASE + 0x10, FI_DATA_REG);          // DMA_DST_ADDR
            success &= write_peripheral(DMA_BASE + 0x14, m_len);                // DMA_LEN
            // CTRL Configuration: SRC_INC (1) | DST_INC (0) | START (1 << 8) = 0x101
            success &= write_peripheral(DMA_BASE + 0x08, 0x00000101);           // DMA_CTRL
            
            // Poll DMA Engine status until done
            if (success) success &= poll_peripheral_done(DMA_BASE);

            // Step 2: Initialize Flash Interface to commit the stream
            success &= write_peripheral(FI_BASE + 0x04, m_flash_addr);          // FI_FLASH_ADDR
            success &= write_peripheral(FI_BASE + 0x08, m_len);                 // FI_LEN
            // CTRL Configuration: OP = PROGRAM (1) | START (1 << 8) = 0x101
            success &= write_peripheral(FI_BASE + 0x00, 0x00000101);            // FI_CTRL

            // Step 3: Poll Flash Interface until done
            if (success) success &= poll_peripheral_done(FI_BASE);
        }
        else if (op == 2) { // READ SEQUENCE: Flash -> Data RAM
            // Step 1: Initialize Flash Interface to stage and fetch from core memory
            success &= write_peripheral(FI_BASE + 0x04, m_flash_addr);          // FI_FLASH_ADDR
            success &= write_peripheral(FI_BASE + 0x08, m_len);                 // FI_LEN
            // CTRL Configuration: OP = READ (2) | START (1 << 8) = 0x102
            success &= write_peripheral(FI_BASE + 0x00, 0x00000102);            // FI_CTRL

            // Step 2: Program DMA Engine to drain the Flash's streaming register
            success &= write_peripheral(DMA_BASE + 0x0C, FI_DATA_REG);          // DMA_SRC_ADDR
            success &= write_peripheral(DMA_BASE + 0x10, RAM_BASE + m_ram_addr); // DMA_DST_ADDR
            success &= write_peripheral(DMA_BASE + 0x14, m_len);                // DMA_LEN
            // CTRL Configuration: SRC_INC (0) | DST_INC (1) | START (1 << 8) = 0x102
            success &= write_peripheral(DMA_BASE + 0x08, 0x00000102);           // DMA_CTRL

            // Poll both targets for complete drain synchronization
            if (success) success &= poll_peripheral_done(DMA_BASE);
            if (success) success &= poll_peripheral_done(FI_BASE);
        }
        else if (op == 3) { // ERASE SEQUENCE: Pure Flash Range Erase
            // Step 1: Program Flash Interface registers directly (DMA completely bypassed)
            success &= write_peripheral(FI_BASE + 0x04, m_flash_addr);          // FI_FLASH_ADDR
            success &= write_peripheral(FI_BASE + 0x08, m_len);                 // FI_LEN
            // CTRL Configuration: OP = ERASE (3) | START (1 << 8) = 0x103
            success &= write_peripheral(FI_BASE + 0x00, 0x00000103);            // FI_CTRL

            // Step 2: Poll Flash Interface until sector array updates complete
            if (success) success &= poll_peripheral_done(FI_BASE);
        } else {
            success = false;
        }

        // Finalize transaction states based on execution outcome
        m_status &= ~(1 << 0); // Clear BUSY flag
        if (success) {
            m_status |= (1 << 1);  // Set DONE bit
            pulse_interrupt(false); // Fire successful event pulse
        } else {
            m_status |= (1 << 2);  // Set ERROR bit
            pulse_interrupt(true);  // Fire failure event pulse
        }
    }
}

bool CommandProcessor::write_peripheral(uint32_t addr, uint32_t data)
{
    return m_bus.write(addr, data);
}

bool CommandProcessor::read_peripheral(uint32_t addr, uint32_t &data)
{
    return m_bus.read(addr, data);
}

bool CommandProcessor::poll_peripheral_done(uint32_t base_addr)
{
    uint32_t status_word = 0;
    while (true) {
        // Read STATUS register offset (0x00 for DMA_STATUS, 0x0C for FI_STATUS)
        uint32_t status_reg_addr = (base_addr == DMA_BASE) ? (base_addr + 0x00) : (base_addr + 0x0C);
        
        if (!read_peripheral(status_reg_addr, status_word)) {
            return false; // Fatal bus transport drop out
        }

        // Return false if the downstream component signals an internal error bit [2]
        if (status_word & (1 << 2)) {
            return false; 
        }

        // Break loop and return true if component dropped BUSY [0] and raised DONE [1]
        if (!(status_word & (1 << 0)) && (status_word & (1 << 1))) {
            // W1C step: clear the status bit on the slave so it doesn't leak into subsequent loops
            write_peripheral(status_reg_addr, (1 << 1));
            break; 
        }

        // Cooperatively yield the thread scheduler to allow other masters (like the CPU) onto the bus
        wait(SC_ZERO_TIME);
    }
    return true;
}

void CommandProcessor::pulse_interrupt(bool is_error)
{
    if (is_error) {
        cp_error_out.write(true);
        wait(SC_ZERO_TIME); // Pulse for exactly 1 SystemC delta cycle
        cp_error_out.write(false);
    } else {
        cp_done_out.write(true);
        wait(SC_ZERO_TIME);
        cp_done_out.write(false);
    }
}
