#include<systemc>
#include<cstdint>

using namespace sc_core;
using namespace std;

class InterruptController : public sc_module {

	private : 
		
		// two registers.
		uint32_t IRQCTRL_PENDING; // Read Only : 0x00 : latched interrupt sources, independed of mask
		uint32_t IRQCTRL_MASK; // Read Write : 0x04 : per source enable.
		
		// defined for level sensitivity.
		sc_signal<uint32_t> pending_sig, mask_sig;

		void latch_inputs(){

			if(hi_done_in.read()) IRQCTRL_PENDING |= (1 << 0);
			if(hi_error_in.read()) IRQCTRL_PENDING |= ( 1<<1 );		
			if(fi_done_in.read()) IRQCTRL_PENDING |= ( 1<<2 );
			if(fi_error_in.read()) IRQCTRL_PENDING |= ( 1<<3 );
			if(cp_done_in.read()) IRQCTRL_PENDING |= ( 1<<4 );
			if(cp_error_in.read()) IRQCTRL_PENDING |= ( 1<<5 );

			pending_sig.write(IRQCTRL_PENDING);
		
		}
		
		void update_irq() {
			irq_out.write( (pending_sig.read() & mask_sig.read()) !=0 );
		}

	public : 
		
		sc_in<bool> hi_done_in, hi_error_in;
		sc_in<bool> fi_done_in, fi_error_in;
		sc_in<bool> cp_done_in, cp_error_in;
		
		sc_out<bool> irq_out;

		//SC_HAS_PROCESS(InterruptController);
		InterruptController(sc_module_name name) : sc_module(name), IRQCTRL_PENDING(0), IRQCTRL_MASK(0){
			
			// reset 
			pending_sig.write(0);
			mask_sig.write(0);

			SC_METHOD(latch_inputs);
			sensitive << hi_done_in.pos() << hi_error_in.pos()
				  << fi_done_in.pos() << fi_error_in.pos()
			  	  << cp_done_in.pos() << cp_error_in.pos() ;
	      		
			SC_METHOD(update_irq);
			sensitive << pending_sig << mask_sig ;		
			
		}

		void bus_access(uint32_t addr, uint32_t &data, bool is_write){
			// uint32_t offset = addr - 0x20000300 ;
			switch(addr){
				case 0x00: // pending
					if(!is_write) data = IRQCTRL_PENDING;
					break;

				case 0x04: // mask
					if(is_write){
						IRQCTRL_MASK = data;
						mask_sig.write(IRQCTRL_MASK);
					}
					else {
						data = IRQCTRL_MASK;
					}
					break;

				case 0x08: // clear
					if(is_write){
						IRQCTRL_PENDING &= ~data;
						pending_sig.write(IRQCTRL_PENDING);
					}
					data = 0 ;
					break;

				case 0x0C: // raise
					if(is_write){
						IRQCTRL_PENDING |= data;
						pending_sig.write(IRQCTRL_PENDING);
					}
					data = 0 ;
					break;

				default : 
					data = 0;
					break;
			}
		}

} ;
