#include<systemc>
#include<cstdint>
#include "Bus.h"

#define INTC_BASE 0x20000300
#define INTC_SIZE 256
#define VALID_BITS_MASK 0x0000003F

class InterruptController : public sc_core :: sc_module, public IBusSlave {

	private : 
		
		// two registers.
		uint32_t IRQCTRL_PENDING; // Read Only : 0x00 : latched interrupt sources, independed of mask
		uint32_t IRQCTRL_MASK; // Read Write : 0x04 : per source enable.
		sc_core::sc_event m_update_ev;

	public : 
		
		sc_in<bool> hi_done_in, hi_error_in;
		sc_in<bool> fi_done_in, fi_error_in;
		sc_in<bool> cp_done_in, cp_error_in;
		
		sc_out<bool> irq_out;

//		SC_HAS_PROCESS(InterruptController);
		InterruptController(sc_core::sc_module_name name, Bus &bus);

		void bus_access(uint32_t addr, uint32_t &data, bool is_write);

		void pin_interrupt_handler();
		void update_irq_output();

} ;
