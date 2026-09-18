#ifndef BUS_H
#define BUS_H

#include <systemc.h>
#include <vector>
#include <string>
#include <cstdint>

// Every peripheral IP inherits from this interface
class IBusSlave
{
public:
    virtual ~IBusSlave() = default;
    
    // addr is RELATIVE to this slave's own registered base address
    // The Bus automatically subtracts the base address before invoking this method
    virtual void bus_access(uint32_t addr, uint32_t &data, bool is_write) = 0;
};

// Main Interconnect Class
class Bus : public sc_core::sc_module
{
public:
    explicit Bus(sc_core::sc_module_name name);

    // Registration routine called during slave constructor elaboration
    void register_slave(const std::string &name, uint32_t base, uint32_t size, IBusSlave *slave);

    // Blocking primary transaction method (Must only be called from an SC_THREAD)
    bool transact(uint32_t addr, uint32_t &data, bool is_write);

    // Convenience wrappers over transact()
    bool write(uint32_t addr, uint32_t data);
    bool read(uint32_t addr, uint32_t &data);

   // SC_HAS_PROCESS(Bus);

private:
    // Structure to represent a mapped peripheral region
    struct SlaveRegion {
        std::string name;
        uint32_t base;
        uint32_t size;
        IBusSlave *slave;
    };

    std::vector<SlaveRegion> m_slaves; // Storage for registered address regions
    sc_mutex m_bus_mutex;              // Single arbiter mutex for transaction locking
};

#endif // BUS_H



