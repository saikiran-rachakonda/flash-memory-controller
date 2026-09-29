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
                    uint32_t requested_cmd = data & 0x0Fu;
		    m_cmd = requested_cmd;
                    bool start_strobe = (data & (1u << 8)) ;

                    if (start_strobe) {
                        // ERROR CODE 5: START written while BUSY is active [0x31.33]
                        if (m_status & 0x01u ) {
                            m_status &= ~0x01u;        // Clear previous error codes
                            m_status |= (1u << 2);          // Set ERROR bit
                            m_status = (m_status & ~0xF0u) |  (5u << 4);          // Latch BUSY Error Code (5)
                        }else {
				m_start_req.notify(SC_ZERO_TIME);
			}

                }
                break;

            case 0x004: m_flash_addr = data; break; // HI_FLASH_ADDR [0x31.32]
            case 0x008: m_ram_addr   = data; break; // HI_RAM_ADDR [0x31.32]
            case 0x00C: m_len        = data; break; // HI_LEN [0x31.32]
           
            case 0x010: // HI_STATUS (W1C semantics for bits [1:2]) [0x31.32]
                {
			if(data & (1u << 1)) {
				m_status &= ~(1u << 1);
			}
			if(data & (1u << 2)) {
				m_status &= ~(1u << 2);
				m_status &= ~0xF0u;
			}
		    break;
                }

            case 0x014: m_irq_enable = data & 0x03u; break; // HI_IRQ_ENABLE [0x31.32]
            default: break;
        }
    } else {
        // Read Transaction Routine Mapping [0x31.32]
        switch (addr) {
            case 0x000: data = m_cmd & 0x0Fu;        break; // START bit always returns 0
            case 0x04: data = m_flash_addr; break;
            case 0x08: data = m_ram_addr;   break;
            case 0x0C: data = m_len;        break;
            case 0x010: data = m_status;     break;
            case 0x014: data = m_irq_enable; break;
            default:   data = 0;            break;
        }
    }
}

uint32_t HostInterface::validate_command(uint32_t cmd){
	if(cmd>3u){
		return 4u;
	}
	if(cmd==0u){
		return 0u;
	}
	if(m_len == 0u || m_len > RAM_MAX_SIZE) {
		return 3u;
	}
	if(cmd == 1u || cmd == 2u ) {
		if((uint64_t)m_ram_addr + m_len > RAM_MAX_SIZE){
			return 3u;
		}
	}
	if((uint64_t)m_flash_addr + m_len > FLASH_MAX_SIZE){
		return 1u;
	}
	if(cmd == 3u){
		if((m_flash_addr % SECTOR_SIZE != 0) || (m_len % SECTOR_SIZE != 0)){
			return 2u;
		}
	}
	
	return 0u;
}

void HostInterface::orchestrate_thread()
{
    while (true) {
        // Sleep until bus_access notifies a successful command start request [0x31.33]
        wait(m_start_req);
	
	uint32_t cmd = m_cmd & 0x0Fu;
	uint32_t err_code = validate_command(cmd);

        if (err_code != 0u) { // Skip processing if opcode is a NOP [0x31.33]
        	m_status &= ~0x01u;
		m_status |= (1u << 2);
		m_status = (m_status & ~0xF0u) | (err_code << 4);
		pulse(hi_error_out, (m_irq_enable & (1u << 1))!=0);
		continue;
	}
	if(cmd == 0u){
		m_status &= ~0x01u;
		continue;
	}

	m_status |= 0x01u;

	m_bus.write(CP_BASE + CP_FLASH_ADDR_OFF, m_flash_addr);
	m_bus.write(CP_BASE + CP_RAM_ADDR_OFF, m_ram_addr);
	m_bus.write(CP_BASE + CP_LEN_OFF, m_len);
	m_bus.write(CP_BASE + CP_CTRL_OFF, cmd | 0x100u);

	uint32_t cp_status = 0;
	do{
		wait(10, SC_NS);
		m_bus.read(CP_BASE + CP_STATUS_OFF, cp_status);
	}while((cp_status & 0x06u) == 0);

	m_bus.write(CP_BASE + CP_STATUS_OFF, cp_status & 0x06u);

	m_status &= ~0x01u;

	if(cp_status & (1u << 2)){
		m_status |= (1u<<2);
		pulse(hi_error_out, (m_irq_enable & (1u << 1))!=0);
	}else{
		m_status |= (1u << 1);
		pulse(hi_done_out, (m_irq_enable & (1u << 0))!=0);
	}
    }
}

void HostInterface::pulse(sc_out<bool> &pin, bool enabled){
	if(!enabled) { return; }
	pin.write(true);
	wait(SC_ZERO_TIME);
	pin.write(false);
}
