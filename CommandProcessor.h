#ifndef COMMAND_PROCESSOR_H
#define COMMAND_PROCESSOR_H

#include <systemc.h>
#include <cstdint>
#include "Bus.h"

#define CP_BASE  0x20000500
#define CP_SIZE 256
#define RAM_BASE  0x30000000
#define DMA_BASE  0x20000200
#define FI_BASE  0x20000400
#define FI_DATA_REG (FI_BASE + 0x10) 

class CommandProcessor : public sc_core::sc_module, public IBusSlave
{
public:

    // Dedicated Aggregated Pulse Output Pins
    sc_out<bool> cp_done_out;
    sc_out<bool> cp_error_out;

    // Constructor
    CommandProcessor(sc_core::sc_module_name name, Bus &bus);

    // Bus interface override
    void bus_access(uint32_t addr, uint32_t &data, bool is_write) override;

    // Sequencing Finite State Machine (FSM) Thread
    void fsm_thread();

    //SC_HAS_PROCESS(CommandProcessor);

private:
    Bus &m_bus; // Shared system bus master reference

    // Internal Register State
    uint32_t m_ctrl;
    uint32_t m_flash_addr;
    uint32_t m_ram_addr;
    uint32_t m_len;
    uint32_t m_status;

    // Orchestration Events
    sc_event m_start_sequence_ev;

    // Helper functions to interact with peripheral registers over the bus fabric
    bool write_peripheral(uint32_t addr, uint32_t data);
    bool read_peripheral(uint32_t addr, uint32_t &data);
    bool poll_peripheral_done(uint32_t base_addr);
    void pulse_interrupt(bool is_error);
};

#endif // COMMAND_PROCESSOR_H

