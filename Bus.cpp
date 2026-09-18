#include "Bus.h"

Bus::Bus(sc_core::sc_module_name name) : sc_core::sc_module(name)
{
    // Bus module initialization
}

void Bus::register_slave(const std::string &name, uint32_t base, uint32_t size, IBusSlave *slave)
{
    // CRITICAL: Overlapping window checks at ELABORATION time (Section 2.1)
    uint32_t new_end = base + size;

    for (const auto &region : m_slaves) {
        uint32_t existing_end = region.base + region.size;
        
        // Check for boundary collision
        if ((base < existing_end) && (new_end > region.base)) {
            std::string err_msg = "Bus Elaboration Error: Overlapping window detected between '" 
                                  + name + "' and '" + region.name + "'";
            SC_REPORT_FATAL("BUS_REGISTRATION", err_msg.c_str());
        }
    }

    // Insert safely into mapping vector
    m_slaves.push_back({name, base, size, slave});
}

bool Bus::transact(uint32_t addr, uint32_t &data, bool is_write)
{
    // CRITICAL: SystemC API enforcement checking (Section 3.1)
    // transact() calls m_bus_mutex.lock() which internally calls wait(). 
    // Triggering this from an SC_METHOD causes a runtime abort.
    if (sc_core::sc_get_current_process_handle().proc_kind() == sc_core::SC_METHOD_PROC_) {
        SC_REPORT_ERROR("BUS_PROTOCOL_VIOLATION", "transact() called from an SC_METHOD process!");
    }

    // Acquire lock. Only one master process can execute a transaction at a given moment
    m_bus_mutex.lock();

    IBusSlave *target_slave = nullptr;
    uint32_t relative_addr = 0;

    // Linear decode scan of registered segments (Section 4)
    for (const auto &region : m_slaves) {
        if (addr >= region.base && addr < (region.base + region.size)) {
            target_slave = region.slave;
            relative_addr = addr - region.base; // Subtract base automatically
            break;
        }
    }

    // Unmapped Address handling
    if (!target_slave) {
        if (!is_write) {
            data = 0; // Forced to 0 on unmapped reads
        }
        m_bus_mutex.unlock();
        return false; // Return false silently per requirements contract
    }

    // Execute the slave's register handler while holding the mutex
    target_slave->bus_access(relative_addr, data, is_write);

    // Release bus occupancy allocation
    m_bus_mutex.unlock();
    return true;
}

bool Bus::write(uint32_t addr, uint32_t data)
{
    return transact(addr, data, true);
}

bool Bus::read(uint32_t addr, uint32_t &data)
{
    return transact(addr, data, false);
}

