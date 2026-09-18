#ifndef RAM_H
#define RAM_H

#include <systemc>
#include <cstdint>
#include "Bus.h"

#define RAM_SIZE 4096 // 4 KB = 4 * 1024 Bytes of addresses.
#define BUS_WIDTH 4  // 32 bits wide
#define DEFAULT_VALUE_RAM  0XFFFFFFFF // 32 bits wide
#define RAM_BASE 0x30000000 // base address assigned to RAM IP.

class Ram : public sc_core::sc_module, public IBusSlave {

	private :
	       	
		uint32_t buffer[RAM_SIZE/BUS_WIDTH];

	public : 
		
//		SC_HAS_PROCESS(Ram);

		Ram (sc_core::sc_module_name name, Bus &bus);

		void bus_access(uint32_t addr, uint32_t &data, bool is_write);
};

#endif
