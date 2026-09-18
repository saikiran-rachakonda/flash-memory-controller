#include "InterruptController.h"


InterruptController :: InterruptController(sc_core :: sc_module_name name, Bus &bus) 
	: sc_core::sc_module(name), 
	  IRQCTRL_PENDING(0), 
	  IRQCTRL_MASK(0)
{
			
	bus.register_slave("InterruptController", INTC_BASE, INTC_SIZE, this);

	SC_METHOD(pin_interrupt_handler);
	sensitive << hi_done_in.pos() << hi_error_in.pos()
		  << fi_done_in.pos() << fi_error_in.pos()
		  << cp_done_in.pos() << cp_error_in.pos() ;
	dont_initialize();

	SC_THREAD(update_irq_output);	
	//sensitive << m_update_ev ;			
}

		
void InterruptController :: bus_access(uint32_t addr, uint32_t &data, bool is_write){
			
	if(addr >= INTC_SIZE){
		if(!is_write) data = 0 ;
		return ;
	}
	
	if(is_write){
	
		uint32_t incoming_data = data & VALID_BITS_MASK;

		switch(addr){
			case 0x00 :
				std::cout << "write to the register IRQCTRL_PENDING is not allowed."<< endl;
				break;
			case 0x04 :
				IRQCTRL_MASK = incoming_data;
				update_irq_output();
				break;
			case 0x08 :
				IRQCTRL_PENDING &= ~incoming_data;
				update_irq_output();
				break;
			case 0x0C :
				IRQCTRL_PENDING |= incoming_data;
				update_irq_output();
				break;
			default : 
				break;
		}
		m_update_ev.notify(SC_ZERO_TIME);
	}
	else {
		switch(addr){
			case 0x00:
				data = IRQCTRL_PENDING;
				break;
			case 0x04:
				data = IRQCTRL_MASK;
				break;
			case 0x08:
			case 0x0C:
			default:
				data = 0 ;
				break;
		}

	}	

}

void InterruptController :: pin_interrupt_handler(){

	if(hi_done_in.read() == true ) IRQCTRL_PENDING |= (1<<0);
	if(hi_error_in.read() == true ) IRQCTRL_PENDING |= (1<<1);
	if(fi_done_in.read() == true ) IRQCTRL_PENDING |= (1<<2);
	if(fi_error_in.read() == true ) IRQCTRL_PENDING |= (1<<3);
	if(cp_done_in.read() == true ) IRQCTRL_PENDING |= (1<<4);
	if(cp_error_in.read() == true ) IRQCTRL_PENDING |= (1<<5);
	m_update_ev.notify(SC_ZERO_TIME);
}

void InterruptController :: update_irq_output(){
	irq_out.write(false);
	while(true){
		wait(m_update_ev);
		bool level_out = ( (IRQCTRL_PENDING & IRQCTRL_MASK) != 0 ) ;
		irq_out.write(level_out);
	}
}
