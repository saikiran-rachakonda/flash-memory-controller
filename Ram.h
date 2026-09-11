#ifndef RAM_H
#define RAM_H

#include <systemc>
#include <vector>
#include <cstdint>

using namespace sc_core;
using namespace std;

uint32_t RAM_SIZE = 4096;
uint32_t BUS_WIDTH = 4 ; // 32 bits wide

class Ram : public sc_module {
	private :
	       	
		vector<uint8_t> buffer;
	
	public : 
		SC_HAS_PROCESS(Ram);		
		Ram (sc_module_name name) : sc_module(name), buffer(RAM_SIZE,0){
		}

		void bus_access(uint32_t addr, uint32_t &data, bool is_write){
		
			// first we need to check whether the addr is valid or not.
	
			if( (addr>=RAM_SIZE) | (addr+BUS_WIDTH > RAM_SIZE) ) {
				
				if(!is_write) data = 0; 
				return ;
			}
		
			// are we going to write ?
			if(is_write){
				
				// bit manipulation came into picture to get 8 bits from 32 bit.
				 
				buffer[addr] = data & 0xFF ;
				buffer[addr + 1] = (data>>8) & 0xFF ;
				buffer[addr + 2] = (data>>16) & 0xFF ;
				buffer[addr + 3] = (data>>24) & 0xFF ;
			
			}
		
			// are we going to read ?
			else{
				
				// why casting is needed,
				// because the RAM stores 8 bits, but we need 32 bit data to handle.	
				
				data =	static_cast<uint32_t>(buffer[addr]) | 
					static_cast<uint32_t>(buffer[addr+1]<<8) | 
					static_cast<uint32_t>(buffer[addr+2]<<16) | 
					static_cast<uint32_t>(buffer[addr+3]<<24);
			
			}
		
		}
};

#endif
