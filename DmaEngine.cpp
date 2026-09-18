#include "DmaEngine.h"

DmaEngine::DmaEngine(sc_core::sc_module_name name, Bus &bus)
    : sc_core::sc_module(name),
      m_bus(bus),
      m_status(0), m_bytes_moved(0), m_ctrl(0),
      m_src_addr(0), m_dst_addr(0), m_len(0)
{
    // Register slave window onto system bus
    m_bus.register_slave("dma_engine", DMA_BASE, DMA_SIZE, this);

    // Register master execution thread
    SC_THREAD(move_thread);
}

void DmaEngine::bus_access(uint32_t addr, uint32_t &data, bool is_write)
{
    if (addr >= DMA_SIZE) {
        if (!is_write) data = 0;
        return;
    }

    if (is_write) {
        switch (addr) {
            case 0x00: // DMA_STATUS (W1C semantics for bits [1:2])
                {
                    uint32_t clear_mask = data & 0x00000006;
                    m_status &= ~clear_mask;
                    // Clearing ERROR [2] explicitly clears ERROR_CODE [7:4]
                    if (clear_mask & (1 << 2)) {
                        m_status &= 0x0000000F; // Clear upper error code bits
                    }
                }
                break;

            case 0x04: // DMA_BYTES_MOVED (Read-Only)
                break;

            case 0x08: // DMA_CTRL
                // Store configuration bits [1:0] (SRC_INC, DST_INC)
                m_ctrl = data & 0x00000003; 

                // Check for START strobe bit [8]
                if (data & (1 << 8)) {
                    // CRITICAL: Synchronously clear DONE/ERROR, clear byte counter,
                    // and raise BUSY immediately before waking the thread
                    m_status &= ~0x00000006; // Clear DONE [1] and ERROR [2]
                    m_status |= (1 << 0);    // Set BUSY [0]
                    m_bytes_moved = 0;       // Reset transferred bytes counter

                    // Trigger the master thread to step onto the bus safely
                    m_start_transfer_ev.notify(SC_ZERO_TIME);
                }
                break;

            case 0x0C: m_src_addr = data; break;
            case 0x10: m_dst_addr = data; break;
            case 0x14: m_len = data;      break;
            default:   break;
        }
    } else {
        // Read transactions
        switch (addr) {
            case 0x00: data = m_status;      break;
            case 0x04: data = m_bytes_moved; break;
            case 0x08: data = m_ctrl;        break; // START bit always reads as 0
            case 0x0C: data = m_src_addr;    break;
            case 0x10: data = m_dst_addr;    break;
            case 0x14: data = m_len;         break;
            default:   data = 0;             break;
        }
    }
}

void DmaEngine::move_thread()
{
    while (true) {
        // Sleep until bus_access notifies a START instruction
        wait(m_start_transfer_ev);

        bool src_inc = (m_ctrl & (1 << 0)) != 0;
        bool dst_inc = (m_ctrl & (1 << 1)) != 0;

        uint32_t current_src = m_src_addr;
        uint32_t current_dst = m_dst_addr;
        uint32_t total_len   = m_len;

        bool transfer_error = false;

        // Perform block movement word-by-word (4 bytes at a time)
        for (uint32_t offset = 0; offset < total_len; offset += 4) {
            uint32_t data_word = 0;

            // 1. Execute Bus Read Transaction
            bool read_ok = m_bus.read(current_src, data_word);
            if (!read_ok) {
                transfer_error = true;
                m_status |= (1 << 2); // Set ERROR bit [2]
                m_status |= (DMA_ERR_BUS << 4); // Latch Bus Error code [7:4]
                break; // Abort transfer immediately
            }

            // 2. Execute Bus Write Transaction
            bool write_ok = m_bus.write(current_dst, data_word);
            if (!write_ok) {
                transfer_error = true;
                m_status |= (1 << 2); // Set ERROR bit [2]
                m_status |= (DMA_ERR_BUS << 4); // Latch Bus Error code [7:4]
                break; // Abort transfer immediately
            }

            // CRITICAL: Increment byte counter INSIDE the loop to support abort coverage
            m_bytes_moved += 4;

            // Apply conditional per-endpoint step behaviors
            if (src_inc) current_src += 4;
            if (dst_inc) current_dst += 4;
        }

        // Finalize transaction states
        m_status &= ~(1 << 0); // Clear BUSY [0]
        m_status |= (1 << 1);  // Set DONE [1]
    }
}
