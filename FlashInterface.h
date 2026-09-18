#ifndef FLASH_INTERFACE_H
#define FLASH_INTERFACE_H

#include <systemc.h>
#include <vector>
#include <string>
#include <cstdint>
#include "Bus.h"

#define FI_BASE 0x20000400
#define FI_SIZE 256
#define FLASH_SIZE 1u * 1024 * 1024      // 1 MB
#define FLASH_SIZE_WORDS (FLASH_SIZE/4)
#define FLASH_SECTOR_SIZE 4u * 1024  // 4 KB
#define FLASH_SECTOR_SIZE_WORDS (FLASH_SECTOR_SIZE/4)
#define DEFAULT_VALUE_FLASH 0xFFFFFFFF

class FlashInterface : public sc_core::sc_module, public IBusSlave
{
public:

    // Ports
    sc_out<bool> fi_done_out;
    sc_out<bool> fi_error_out;

    // Constructor
    FlashInterface(sc_core::sc_module_name name, Bus &bus, std::string image_path);
    ~FlashInterface();

    // Bus access interface override
    void bus_access(uint32_t addr, uint32_t &data, bool is_write) override;

    // Separate SC_THREAD to safely handle the one-delta interrupt pulses
    void interrupt_pulse_thread();

    //SC_HAS_PROCESS(FlashInterface);

private:
    // Internal register state storage
    uint32_t m_ctrl;
    uint32_t m_flash_addr;
    uint32_t m_len;
    uint32_t m_status;

    // Flash persistent memory buffer and file control
    std::string m_image_path;
    std::vector<uint32_t> m_flash_mem;

    // Streaming state tracking
    uint32_t m_word_counter;
    uint32_t m_total_words_expected;

    // Internal synchronization events
    sc_event m_pulse_done_ev;
    sc_event m_pulse_error_ev;

    // Helper functions
    void load_flash_image();
    void save_flash_image();
    void execute_erase();
    void check_and_finalize_stream();
};

#endif // FLASH_INTERFACE_H
