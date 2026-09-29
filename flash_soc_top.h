// =====================================================================
// IP-09  FLASH CONTROLLER SoC -- TOP LEVEL
//
// THIS HEADER IS THE CONTRACT. DO NOT EDIT IT.
// You implement src/flash_soc_top.cc.
// =====================================================================
//
// FlashSocTop is the whole SoC in one module. It owns every IP, and it
// is responsible for the two kinds of connection that the Bus does NOT
// make for you:
//
//   (a) the ten interrupt pulse pins, IP -> Interrupt Controller, and
//       the Controller's single level output;
//   (b) the two serial byte channels that carry the host link in and
//       out of the Host UART.
//
// Everything else -- every register access between IPs -- travels over
// the Bus, and each IP registers its own slave window from its own
// constructor. You do not wire those.
//
// WHY THE CONSTRUCTION ORDER MATTERS
// Every IP takes `Bus &` and calls bus.register_slave() from its own
// constructor. C++ builds members in DECLARATION order, so m_bus must
// be declared before every IP below it. It is. Leave it that way.
//
// This header is deliberately free of QBox/QEMU dependencies so it
// builds with nothing but SystemC. The QBox platform wrapper is a
// separate concern and is not part of this exercise.
#ifndef FLASH_SOC_TOP_H
#define FLASH_SOC_TOP_H

#include <systemc>
#include <cstdint>
#include <string>

#include "bus.h"
#include "ram.h"
#include "flash_interface.h"
#include "dma_engine.h"
#include "command_processor.h"
#include "host_interface.h"
#include "interrupt_controller.h"
#include "host_uart.h"
#include "host_protocol_engine.h"

class FlashSocTop : public sc_core::sc_module
{
public:
    // ---- the SoC's external face -------------------------------------
    //
    // The host link's wire. A testbench (or a platform) binds its own
    // HostModel to these. Depth 64 is plenty: the UART throttles at
    // UART_CHAR_NS per byte, so the fifo never becomes the bottleneck.
    sc_core::sc_fifo<unsigned char> host_to_soc;
    sc_core::sc_fifo<unsigned char> soc_to_host;

    // The Interrupt Controller's single level output, brought to the
    // boundary. On a real platform this goes to the CPU's external
    // interrupt input; here the testbench and the Dummy CPU watch it.
    sc_core::sc_signal<bool> irq_out;

    SC_HAS_PROCESS(FlashSocTop);

    // `flash_image` is handed straight to the Flash Interface, which
    // backs the flash array with that file so contents survive a run.
    FlashSocTop(sc_core::sc_module_name name,
                const std::string &flash_image = "flash_tb.img");

    // ---- test hooks ---------------------------------------------------
    // The testbench and the Dummy CPU act as bus masters through this,
    // exactly as the real CPU would. Not a back door: the CPU reaches
    // the SoC over the bus too.
    Bus &bus() { return m_bus; }

    // Frame counters, for the testbench's end-of-run check.
    uint32_t frames_ok()  const { return m_pe.frames_ok(); }
    uint32_t frames_err() const { return m_pe.frames_err(); }

private:
    // ---- DECLARATION ORDER IS CONSTRUCTION ORDER. Bus first. --------
    Bus                 m_bus;
    Ram                 m_ram;
    FlashInterface      m_fi;
    DmaEngine           m_dma;
    CommandProcessor    m_cp;
    HostInterface       m_hi;
    InterruptController m_irq;
    HostUart            m_uart;
    HostProtocolEngine  m_pe;

    // ---- the ten interrupt pulse pins -------------------------------
    // Named exactly as in 00-spec.md's interrupt-source table, so the
    // bit index in IRQ_PENDING and the signal name line up.
    sc_core::sc_signal<bool> m_hi_done, m_hi_err;   // PENDING[0], [1]
    sc_core::sc_signal<bool> m_fi_done, m_fi_err;   // PENDING[2], [3]
    sc_core::sc_signal<bool> m_cp_done, m_cp_err;   // PENDING[4], [5]
    sc_core::sc_signal<bool> m_ua_rx,   m_ua_err;   // PENDING[6], [7]
    sc_core::sc_signal<bool> m_pe_done, m_pe_err;   // PENDING[8], [9]
};

#endif // FLASH_SOC_TOP_H
