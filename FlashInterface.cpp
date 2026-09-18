#include "FlashInterface.h"
#include <fstream>
#include <cmath>
#include <algorithm>

FlashInterface::FlashInterface(sc_core::sc_module_name name, Bus &bus, std::string image_path)
    : sc_core::sc_module(name),
      fi_done_out("fi_done_out"),
      fi_error_out("fi_error_out"),
      m_ctrl(0), m_flash_addr(0), m_len(0), m_status(0),
      m_image_path(image_path),
      m_flash_mem(FLASH_SIZE_WORDS, DEFAULT_VALUE_FLASH), // Fresh runs default to all 0xFFFFFFFF
      m_word_counter(0), m_total_words_expected(0)
{
    // Register slave window onto system bus
    bus.register_slave("flash_interface", FI_BASE, FI_SIZE, this);

    // Load existing persistent data if available
    load_flash_image();

    // Initialize output pins to false at time zero (Acceptance Criteria #4.4)
    fi_done_out.initialize(false);
    fi_error_out.initialize(false);

    // Register the interrupt generator thread
    SC_THREAD(interrupt_pulse_thread);
}

FlashInterface::~FlashInterface()
{
    // Guarantee flush on simulator destruction
    save_flash_image();
}

void FlashInterface::bus_access(uint32_t addr, uint32_t &data, bool is_write)
{
    if (addr >= FI_SIZE) {
        if (!is_write) data = 0;
        return;
    }

    if (is_write) {
        switch (addr) {
            case 0x00: { // FI_CTRL
                m_ctrl = data & 0x0000000F; // Extract OP field [3:0]
                
                if (data & (1 << 8)) { // START bit [8] detected
                    m_word_counter = 0;
                    m_total_words_expected = static_cast<uint32_t>(std::ceil(static_cast<double>(m_len) / 4.0));
                    
                    uint32_t op = m_ctrl & 0xF;
                    if (op == 3) { // ERASE Operation
                        m_status |= (1 << 0); // Set BUSY [0]
                        execute_erase();
                        m_status &= ~(1 << 0); // Clear BUSY [0]
                        m_status |= (1 << 1);  // Set DONE [1]
                        m_pulse_done_ev.notify(SC_ZERO_TIME);
                    } 
                    else if (op == 1 || op == 2) { // PROGRAM or READ
                        m_status |= (1 << 0);   // Set BUSY [0]
                        m_status &= ~(1 << 1);  // Clear DONE [1]
                        
                        // Edge case: length is 0, complete immediately
                        if (m_total_words_expected == 0) {
                            m_status &= ~(1 << 0);
                            m_status |= (1 << 1);
                            m_pulse_done_ev.notify(SC_ZERO_TIME);
                        }
                    }
                }
                break;
            }
            case 0x04: // FI_FLASH_ADDR
                m_flash_addr = data;
                break;
            case 0x08: // FI_LEN
                m_len = data;
                break;
            case 0x0C: // FI_STATUS (W1C semantics for bits [1:2])
                m_status &= ~(data & 0x00000006);
                break;
            case 0x10: { // FI_DATA (Streaming Write - PROGRAM)
                if ((m_status & (1 << 0)) && ((m_ctrl & 0xF) == 1)) {
                    	
			uint32_t target_word_idx = (m_flash_addr/4) + m_word_counter;
			if(target_word_idx < FLASH_SIZE_WORDS){
				m_flash_mem[target_word_idx] &= data;
			}
			m_word_counter++;
                    	check_and_finalize_stream();
                }
                break;
            }
            default:
                break;
        }
    } else {
        // Read handling
        switch (addr) {
            case 0x00: data = m_ctrl; break; // START bit always reads back as 0
            case 0x04: data = m_flash_addr; break;
            case 0x08: data = m_len; break;
            case 0x0C: data = m_status; break;
            case 0x10: { // FI_DATA (Streaming Read - READ)
                data = 0;
                if ((m_status & (1 << 0)) && ((m_ctrl & 0xF) == 2)) {
                    uint32_t target_word_idx = (m_flash_addr/4) + m_word_counter;
                        if (target_word_idx < FLASH_SIZE_WORDS) {
                            data = m_flash_mem[target_word_idx];
                        } else {
                            data = 0xFFFFFFFF;
                        }
                    m_word_counter++;
                    check_and_finalize_stream();
                }
                break;
            }
            default: data = 0; break;
        }
    }
}

void FlashInterface::check_and_finalize_stream()
{
    if (m_word_counter >= m_total_words_expected) {
        m_status &= ~(1 << 0); // Clear BUSY
        m_status |= (1 << 1);  // Set DONE
        
        save_flash_image(); // Persist changes directly to file system
        m_pulse_done_ev.notify(SC_ZERO_TIME);
    }
}

void FlashInterface::execute_erase()
{
    uint32_t start_addr = m_flash_addr/ FLASH_SECTOR_SIZE;
    uint32_t end_addr = ( m_flash_addr + m_len-1 ) / FLASH_SECTOR_SIZE ;
	
	for(uint32_t sector = start_addr; sector<= end_addr; ++sector){
		uint32_t start_word_idx = sector * FLASH_SECTOR_SIZE_WORDS;
		if(start_word_idx + FLASH_SECTOR_SIZE_WORDS <= FLASH_SIZE_WORDS){
			std::fill_n(m_flash_mem.begin() + start_word_idx, FLASH_SECTOR_SIZE_WORDS, DEFAULT_VALUE_FLASH);
		}
	}

    save_flash_image(); // Instantly write out to prevent caching drop-outs
}

void FlashInterface::interrupt_pulse_thread()
{
    while (true) {
        wait(m_pulse_done_ev | m_pulse_error_ev);
        
        if (m_status & (1 << 1)) { // DONE pulse generated
            fi_done_out.write(true);
            wait(SC_ZERO_TIME); // Hold for exactly 1 SystemC Delta cycle
            fi_done_out.write(false);
        } else if (m_status & (1 << 2)) { // ERROR pulse generated
            fi_error_out.write(true);
            wait(SC_ZERO_TIME);
            fi_error_out.write(false);
        }
    }
}

void FlashInterface::load_flash_image()
{
    std::ifstream file(m_image_path, std::ios::binary);
    if (file) {
        file.read(reinterpret_cast<char*>(m_flash_mem.data()), FLASH_SIZE);
    }
    // If file doesn't exist, it retains default constructor 0xFF initialization state
}

void FlashInterface::save_flash_image()
{
    std::ofstream file(m_image_path, std::ios::binary | std::ios::trunc);
    if (file) {
        file.write(reinterpret_cast<const char*>(m_flash_mem.data()), FLASH_SIZE);
        file.flush();
    }
}

