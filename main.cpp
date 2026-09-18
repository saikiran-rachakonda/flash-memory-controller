#include <systemc.h>
#include <cassert>
#include "Bus.h"
#include "Ram.h"
#include "InterruptController.h"
#include "FlashInterface.h"
#include "DmaEngine.h"
#include "CommandProcessor.h"
#include "HostInterface.h"

// Test runner process thread
class Testbench : public sc_core::sc_module {
public:
    Bus &m_bus;
   
    //SC_HAS_PROCESS(Testbench);
    Testbench(sc_core::sc_module_name name, Bus &bus) : sc_core::sc_module(name), m_bus(bus) {
        SC_THREAD(run_tests);
    }

    void run_tests() {
        cout << "\n=== STARTING SOCO MODULE TESTS ===\n" << endl;

        test_ip01_ram();
       	test_ip02_intc();
        test_ip03_flash();
        test_ip05_dma();

        cout << "\n=== ALL MODULE INTEGRATION TESTS PASSED SUCCESSFULLY ===\n" << endl;
        sc_stop();
    }

private:
    // --- IP-01: RAM TEST BLOCK ---
    void test_ip01_ram() {
        cout << "[Test IP-01] Testing Data RAM reads and writes..." << endl;
        uint32_t write_data = 0xDEADBEEF;
        uint32_t read_data = 0;

        // Write a word to RAM Base offset 0x10
        bool ok = m_bus.write(RAM_BASE + 0x10, write_data);
        assert(ok && "RAM Write failed!");

        // Read it back
        ok = m_bus.read(RAM_BASE + 0x10, read_data);
        assert(ok && "RAM Read failed!");
        assert(read_data == write_data && "RAM Data mismatch!");

        // Out-of-bounds safety check probing past 4 KB boundary
        ok = m_bus.read(RAM_BASE + 5000, read_data);
        assert(read_data == 0 && "Out-of-bounds RAM probe should safely return 0!");
        cout << "[Test IP-01] RAM checks passed successfully.\n" << endl;
    }

    // --- IP-02: INTC TEST BLOCK ---
    void test_ip02_intc() {
        cout << "[Test IP-02] Testing Interrupt Controller software paths..." << endl;
        uint32_t read_data = 0;

        // Write to MASK register (Offset 0x04) to enable interrupt bit 0
        m_bus.write(INTC_BASE + 0x04, 0x1);

        // Strobe the RAISE register (Offset 0x0C) to trigger software-only testing interrupt
        m_bus.write(INTC_BASE + 0x0C, 0x1);

        // Read PENDING register (Offset 0x00)
        m_bus.read(INTC_BASE + 0x00, read_data);
        assert((read_data & 0x1) == 0x1 && "Pending interrupt bit 0 not latched!");
       
        // Clear the interrupt using Write-1-To-Clear strobe (Offset 0x08)
        m_bus.write(INTC_BASE + 0x08, 0x1);
        m_bus.read(INTC_BASE + 0x00, read_data);
        assert((read_data & 0x1) == 0 && "Pending interrupt failed to clear via W1C!");
        cout << "[Test IP-02] INTC register mapping tracks perfectly.\n" << endl;
    }

    // --- IP-03: FLASH TEST BLOCK ---
    void test_ip03_flash() {
        cout << "[Test IP-03] Testing Flash non-volatile logical rules..." << endl;
       
        // 1. Initial sector ERASE (Command 3)
        m_bus.write(FI_BASE + 0x04, 0x0000); // FI_FLASH_ADDR
        m_bus.write(FI_BASE + 0x08, 4096);   // FI_LEN
        m_bus.write(FI_BASE + 0x00, 0x103);  // START | OP=3 (ERASE)
        wait(1, SC_NS); // Yield delta for immediate finalization

        // 2. Program 0x55555555 over pristine erased memory
	m_bus.write(FI_BASE + 0x04, 0x0000);
	m_bus.write(FI_BASE + 0x08, 4);
        m_bus.write(FI_BASE + 0x00, 0x101);  // START | OP=1 (PROGRAM)
        m_bus.write(FI_BASE + 0x10, 0x55555555); // Streaming Write to FI_DATA
        wait(1, SC_NS);

        // 3. Program 0x33333333 on top of it without erasing (Enforcing the bitwise AND rule)
	m_bus.write(FI_BASE + 0x04, 0x0000);
	m_bus.write(FI_BASE + 0x08, 4);
        m_bus.write(FI_BASE + 0x00, 0x101);
        m_bus.write(FI_BASE + 0x10, 0x33333333);
        wait(1, SC_NS);

        // 4. Verify results over streaming READ (Command 2)
	m_bus.write(FI_BASE + 0x04, 0x0000);
	m_bus.write(FI_BASE + 0x08, 4);
        m_bus.write(FI_BASE + 0x00, 0x102);  // START | OP=2 (READ)
        uint32_t flash_out = 0;
        m_bus.read(FI_BASE + 0x10, flash_out); // Read back from FI_DATA

        // Expected result: 0x55555555 AND 0x33333333 = 0x11111111
        assert(flash_out == (0x55555555 & 0x33333333));
        cout << "[Test IP-03] Flash bitwise AND constraint validation passed.\n" << endl;
    }

    // --- IP-05: DMA ENGINE TEST BLOCK ---
    void test_ip05_dma() {
        cout << "[Test IP-05] Testing Master DMA blocks memory movement..." << endl;
       
        // Stage data into RAM offset 0x100
        m_bus.write(RAM_BASE + 0x100, 0xABCDEF01);

        // Program DMA parameters to copy memory-to-memory
        m_bus.write(DMA_BASE + 0x0C, RAM_BASE + 0x100); // DMA_SRC_ADDR
        m_bus.write(DMA_BASE + 0x10, RAM_BASE + 0x200); // DMA_DST_ADDR
        m_bus.write(DMA_BASE + 0x14, 4);                    // DMA_LEN
        m_bus.write(DMA_BASE + 0x08, 0x103);                 // START | SRC_INC | DST_INC

        // Poll DMA_STATUS until BUSY bit (bit 0) falls low
        uint32_t status = 1;
        while (status & 0x1) {
            m_bus.read(DMA_BASE + 0x00, status);
            wait(SC_ZERO_TIME);
        }

        // Verify that the data was actually moved by checking target memory destination
        uint32_t ram_destination_check = 0;
        m_bus.read(RAM_BASE + 0x200, ram_destination_check);
        assert(ram_destination_check == 0xABCDEF01 && "DMA failed to transfer data!");
        cout << "[Test IP-05] DMA engine hardware loops verified correctly.\n" << endl;
    }
};

int sc_main(int argc, char* argv[]) {
    // Shared Internal Bus Interconnect
    Bus system_bus("system_bus");

    // Instantiate all modules we've designed so far
    HostInterface       host_interface("host_interface", system_bus);
    DmaEngine           dma_engine("dma_engine", system_bus);
    InterruptController intc("interrupt_controller", system_bus);
    FlashInterface      flash_interface("flash_interface", system_bus, "test_flash.img");
    CommandProcessor    command_processor("command_processor", system_bus);
    Ram                 data_ram("data_ram", system_bus);

    // Dynamic Interrupt Wire Pin Interconnections (Section 6.1 Contract)
    sc_signal<bool> sig_hi_done, sig_hi_error, sig_fi_done, sig_fi_error, sig_cp_done, sig_cp_error;
    sc_signal<bool, sc_core::SC_MANY_WRITERS> sig_cpu_irq;

    host_interface.hi_done_out(sig_hi_done);         intc.hi_done_in(sig_hi_done);
    host_interface.hi_error_out(sig_hi_error);       intc.hi_error_in(sig_hi_error);
    flash_interface.fi_done_out(sig_fi_done);        intc.fi_done_in(sig_fi_done);
    flash_interface.fi_error_out(sig_fi_error);      intc.fi_error_in(sig_fi_error);
    command_processor.cp_done_out(sig_cp_done);      intc.cp_done_in(sig_cp_done);
    command_processor.cp_error_out(sig_cp_error);    intc.cp_error_in(sig_cp_error);

    // Bind missing/unused pins to dummy targets to prevent elaboration failure
    intc.irq_out(sig_cpu_irq);

    // Instantiate and execute test driver module
    Testbench tb("testbench_driver", system_bus);

    sc_start(); // Trigger SystemC Simulation scheduler
    return 0;
}
