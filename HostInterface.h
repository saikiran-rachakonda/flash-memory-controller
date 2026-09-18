#ifndef HOST_INTERFACE_H
#define HOST_INTERFACE_H

#include <systemc.h>
#include <cstdint>
#include "Bus.h"

class HostInterface : public sc_core::sc_module, public IBusSlave
{
public:
    static constexpr uint32_t HI_BASE = 0x20000000;
    static constexpr uint32_t HI_SIZE = 512;

    // Hardcoded absolute base address for the Command Processor
    static constexpr uint32_t CP_BASE_ADDR = 0x20000500;

    // Dedicated Pulse Interconnect Output Pins [0x31.31]
    sc_out<bool> hi_done_out;
    sc_out<bool> hi_error_out;

    // Constructor [0x31.31]
    HostInterface(sc_core::sc_module_name name, Bus &bus);

    // Bus interface slave method override [0x31.31]
    void bus_access(uint32_t addr, uint32_t &data, bool is_write) override;

    // Orchestration background thread [0x31.31]
    void orchestrate_thread();

    //SC_HAS_PROCESS(HostInterface);

private:
    Bus &m_bus; // Shared system bus master reference [0x31.31]

    // Internal Registers [0x31.32]
    uint32_t m_cmd;
    uint32_t m_flash_addr;
    uint32_t m_ram_addr;
    uint32_t m_len;
    uint32_t m_status;
    uint32_t m_irq_enable;

    // Synchronization Event [0x31.32]
    sc_event m_start_req;

    // Internal Master Transport Wrappers
    bool write_cp_register(uint32_t offset, uint32_t data);
    bool read_cp_register(uint32_t offset, uint32_t &data);
};

#endif // HOST_INTERFACE_H
