#ifndef DMA_ENGINE_H
#define DMA_ENGINE_H

#include <systemc.h>
#include <cstdint>
#include "Bus.h" // Contains Bus and IBusSlave definitions

#define DMA_BASE  0x20000200
#define  DMA_SIZE 256
#define  DMA_ERR_NONE 0
#define DMA_ERR_BUS 1 // Raised if a bus transaction returns false

class DmaEngine : public sc_core::sc_module, public IBusSlave
{
public:

    // Constructor
    DmaEngine(sc_core::sc_module_name name, Bus &bus);

    // Bus interface override
    void bus_access(uint32_t addr, uint32_t &data, bool is_write) override;

    // Decoupled master block data mover process
    void move_thread();

   // SC_HAS_PROCESS(DmaEngine);

private:
    Bus &m_bus; // Reference to master bus interface

    // Internal Registers
    uint32_t m_status;
    uint32_t m_bytes_moved;
    uint32_t m_ctrl;
    uint32_t m_src_addr;
    uint32_t m_dst_addr;
    uint32_t m_len;

    // Synchronization event to wake the data-moving thread
    sc_event m_start_transfer_ev;
};

#endif // DMA_ENGINE_H

