#include "HostInterface.h"

HostInterface::HostInterface(sc_core::sc_module_name name, Bus &bus)
    : sc_core::sc_module(name),
      hi_done_out("hi_done_out"),
      hi_error_out("hi_error_out"),
	m_bus(bus),
      m_cmd(0), m_flash_addr(0), m_ram_addr(0), m_len(0), m_status(0), m_irq_enable(0)
{
    // Register slave window onto system interconnect [0x31.34]
    m_bus.register_slave("host_interface", HI_BASE, HI_SIZE, this);

    // Section 4.1: Drive interrupt pulse lines false at time zero
    hi_done_out.initialize(false);
    hi_error_out.initialize(false);

    // Register primary orchestration processing thread [0x31.34]
    SC_THREAD(orchestrate_thread);
}

void HostInterface::bus_access(uint32_t addr, uint32_t &data, bool is_write)
{
    if (addr >= HI_SIZE) {
        if (!is_write) data = 0;
        return;
    }

    if (is_write) {
        switch (addr) {
            case 0x000: // HI_CMD [0x31.32]
                {
                    uint32_t requested_op = data & 0xF;
                    bool start_strobe = (data & (1 << 8)) != 0;

                    if (start_strobe) {
                        // ERROR CODE 5: START written while BUSY is active [0x31.33]
                        if (m_status & (1 << 0)) {
                            m_status |= (1 << 2);          // Set ERROR bit
                            m_status &= 0x0000000F;        // Clear previous error codes
                            m_status |= (5 << 4);          // Latch BUSY Error Code (5)
                            return;
                        }

                        // Execute Command Validation Paths [0x31.33]
                        uint32_t error_code = 0;

                        // ERROR CODE 4: Check for Invalid Command Opcodes [0x31.33]
                        if (requested_op > 3) {
                            error_code = 4; // BAD_CMD
                        }
                        // ERROR CODE 1: Check Flash Device Boundaries [0x31.33]
                        else if (static_cast<uint64_t>(m_flash_addr) + m_len > (1u * 1024 * 1024)) {
                            error_code = 1; // BAD_ADDR
                        }
                        // ERROR CODE 2: Check for Erase Sector Alignment [0x31.33]
                        else if (requested_op == 3 && ((m_flash_addr % 4096 != 0) || (m_len % 4096 != 0))) {
                            error_code = 2; // UNALIGNED_ERASE
                        }
                        // ERROR CODE 3: Check for Length/RAM Offset Range Infractions [0x31.33]
                        else if (m_len == 0 || m_len > 4096 ||
                                ((requested_op == 1 || requested_op == 2) && (static_cast<uint64_t>(m_ram_addr) + m_len > 4096))) {
                            error_code = 3; // BAD_LEN
                        }

                        // Evaluate validation results [0x31.32]
                        if (error_code != 0) {
                            m_status |= (1 << 2);          // Set ERROR bit
                            m_status &= 0x0000000F;        // Clear old code footprints
                            m_status |= (error_code << 4); // Latch validation code
                            m_status &= ~(1 << 0);         // Section 5: BUSY explicitly stays 0
                        } else {
                            // Validation Passed: Update state fields and awake orchestrator [0x31.32]
                            m_cmd = requested_op;          // Commit command
                            m_status &= ~0x00000006;        // Clear old DONE and ERROR bits
                            m_status |= (1 << 0);          // Raise BUSY status flag
                           
                            // Defer processing via event notification pattern [0x31.33]
                            m_start_req.notify(SC_ZERO_TIME);
                        }
                    } else {
                        // Plain configuration update without START bit set
                        m_cmd = requested_op;
                    }
                }
                break;

            case 0x004: m_flash_addr = data; break; // HI_FLASH_ADDR [0x31.32]
            case 0x008: m_ram_addr   = data; break; // HI_RAM_ADDR [0x31.32]
            case 0x00C: m_len        = data; break; // HI_LEN [0x31.32]
           
            case 0x010: // HI_STATUS (W1C semantics for bits [1:2]) [0x31.32]
                {
                    uint32_t w1c_mask = data & 0x00000006;
                    m_status &= ~w1c_mask;
                    if (w1c_mask & (1 << 2)) {
                        m_status &= 0x0000000F; // Clearing ERROR drops the error code [0x31.32]
                    }
                }
                break;

            case 0x014: m_irq_enable = data & 0x3; break; // HI_IRQ_ENABLE [0x31.32]
            default: break;
        }
    } else {
        // Read Transaction Routine Mapping [0x31.32]
        switch (addr) {
            case 0x000: data = m_cmd;        break; // START bit always returns 0
            case 0x04: data = m_flash_addr; break;
            case 0x08: data = m_ram_addr;   break;
            case 0x0C: data = m_len;        break;
            case 0x010: data = m_status;     break;
            case 0x014: data = m_irq_enable; break;
            default:   data = 0;            break;
        }
    }
}

void HostInterface::orchestrate_thread()
{
    while (true) {
        // Sleep until bus_access notifies a successful command start request [0x31.33]
        wait(m_start_req);

        bool sequence_ok = true;

        if (m_cmd != 0) { // Skip processing if opcode is a NOP [0x31.33]
            // Step 4: Write Command Processor descriptors over the bus fabric [0x31.32]
            sequence_ok &= write_cp_register(0x04, m_flash_addr); // CP_FLASH_ADDR
            sequence_ok &= write_cp_register(0x08, m_ram_addr);   // CP_RAM_ADDR
            sequence_ok &= write_cp_register(0x0C, m_len);        // CP_LEN
           
            // Launch Command Processor: OP field | START strobe bit (1 << 8) [0x31.32]
            uint32_t cp_ctrl_word = m_cmd | (1 << 8);
            sequence_ok &= write_cp_register(0x00, cp_ctrl_word); // CP_CTRL

            // Step 5: Poll CP_STATUS until sequence completes [0x31.32]
            if (sequence_ok) {
                uint32_t cp_status = 0;
                while (true) {
                    if (!read_cp_register(0x10, cp_status)) { // Read CP_STATUS
                        sequence_ok = false;
                        break;
                    }
                   
                    // Exit loop if BUSY[0] falls and DONE[1] or ERROR[2] asserts
                    if (!(cp_status & (1 << 0))) {
                        if (cp_status & (1 << 2)) {
                            sequence_ok = false; // Downstream execution error caught
                        }
                        break;
                    }
                   
                    // Section 8: Always include cooperative yields inside polling loops
                    wait(SC_ZERO_TIME);
                }
            }

            // Step 6: Issue Write-1-to-Clear to release the Command Processor status bit [0x31.32]
            if (sequence_ok) {
                write_cp_register(0x10, (1 << 1)); // Clear DONE
            } else {
                write_cp_register(0x10, (1 << 2)); // Clear ERROR
            }
        }

        // Step 7: Finalize HI_STATUS bits (BUSY drops to 0 on ALL execution paths) [0x31.32, 0x31.33]
        m_status &= ~(1 << 0); // Clear BUSY flag
       
        if (sequence_ok) {
            m_status |= (1 << 1); // Set DONE flag
           
            // Step 8: Assert one-delta completion pulse if enabled [0x31.32]
            if (m_irq_enable & (1 << 0)) { // CMD_DONE_EN
                hi_done_out.write(true);
                wait(SC_ZERO_TIME);
                hi_done_out.write(false);
            }
        } else {
            m_status |= (1 << 2); // Set ERROR flag
           
            if (m_irq_enable & (1 << 1)) { // CMD_ERROR_EN
                hi_error_out.write(true);
                wait(SC_ZERO_TIME);
                hi_error_out.write(false);
            }
        }
    }
}

bool HostInterface::write_cp_register(uint32_t offset, uint32_t data)
{
    return m_bus.write(CP_BASE_ADDR + offset, data);
}

bool HostInterface::read_cp_register(uint32_t offset, uint32_t &data)
{
    return m_bus.read(CP_BASE_ADDR + offset, data);
}
