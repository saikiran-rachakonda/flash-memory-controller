#include "Ram.h"

		
Ram :: Ram (sc_core::sc_module_name name, Bus &bus) : sc_core::sc_module(name){
	for(int i=0;i<(RAM_SIZE/BUS_WIDTH);i++) buffer[i] = DEFAULT_VALUE_RAM;
	bus.register_slave("ram", RAM_BASE, RAM_SIZE, this);				
}



void Ram :: bus_access(uint32_t addr, uint32_t &data, bool is_write){
		
	// first we need to check whether the addr is valid or not.

	if( (addr>=RAM_SIZE) | (addr+BUS_WIDTH > RAM_SIZE) ) {
				
		if(!is_write) data = 0; 
		std::cout<<"the addr is not in the range"<< endl ;
		return ;
	
	}
	
	uint32_t conv_addr = addr / 4 ;
	// are we going to write ?
	if(is_write){
		buffer[conv_addr] = data ;
		std::cout<<"data is written at "<< addr <<" with the value : "<< data << endl;	
	}
		
	// are we going to read ?
	else{
		data =	buffer[conv_addr] ; 
		std::cout<<" data is read from "<< addr <<" value is : "<< data << endl;	
	}
}
