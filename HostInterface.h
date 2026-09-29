#ifndef HOST_INTERFACE_H
#define HOST_INTERFACE_H

#include <systemc.h>
#include <cstdint>
#include "Bus.h"

#define HI_BASE 0x20000000u
#define HI_SIZE 512u

#define CP_BASE 0x20000500u
#define CP_CTRL_OFF 0x00u
#define CP_FLASH_ADDR_OFF 0x04u
#define CP_RAM_ADDR_OFF 0x08u
#define CP_LEN_OFF 0x0Cu
#define CP_STATUS_OFF 0x10u

#define FLASH_MAX_SIZE 0x00100000u
#define RAM_MAX_SIZE 0x00001000u
#define SECTOR_SIZE 0x00001000u


class HostInterface : public sc_core::sc_module, public IBusSlave
{
public:

    // Dedicated Pulse Interconnect Output Pins [0x31.31]
    sc_out<bool> hi_done_out;
    sc_out<bool> hi_error_out;

    // Constructor [0x31.31]
    HostInterface(sc_core::sc_module_name name, Bus &bus);

    // Bus interface slave method override [0x31.31]
    void bus_access(uint32_t addr, uint32_t &data, bool is_write) override;


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

    void orchestrate_thread();
    uint32_t validate_command(uint32_t cmd);
    void pulse(sc_out<bool> &pin, bool enabled);    

};

#endif // HOST_INTERFACE_H
