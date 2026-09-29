// =====================================================================
// IP-09  FLASH CONTROLLER SoC -- TOP LEVEL      *** YOUR DELIVERABLE ***
// =====================================================================

#include "flash_soc_top.h"

using namespace sc_core;

FlashSocTop::FlashSocTop(sc_core::sc_module_name name,
                         const std::string &flash_image)
    : sc_core::sc_module(name)

    // ---- the external face ------------------------------------------
    , host_to_soc("host_to_soc", 64)
    , soc_to_host("soc_to_host", 64)
    , irq_out("irq_out")

    // ---- TODO 1: construct every IP ---------------------------------
    , m_bus("bus")
    , m_ram("ram", m_bus)
    , m_fi("fi", m_bus, flash_image)
    , m_dma("dma", m_bus)
    , m_cp("cp", m_bus)
    , m_hi("hi", m_bus)
    , m_irq("irq", m_bus)
    , m_uart("uart", m_bus)
    , m_pe("pe", m_bus)

    // ---- the ten interrupt pins + the level output ------------------
    , m_hi_done("hi_done"), m_hi_err("hi_err")
    , m_fi_done("fi_done"), m_fi_err("fi_err")
    , m_cp_done("cp_done"), m_cp_err("cp_err")
    , m_ua_rx("ua_rx"),     m_ua_err("ua_err")
    , m_pe_done("pe_done"), m_pe_err("pe_err")
{
    // =================================================================
    // TODO 2: the host link's wire
    // =================================================================
    m_uart.serial_rx( host_to_soc );
    m_uart.serial_tx( soc_to_host );

    // =================================================================
    // TODO 3: the ten interrupt pulse pins
    // =================================================================
    // Host Interface (HI) -> IRQ Controller
    m_hi.hi_done_out(m_hi_done);    m_irq.hi_done_in(m_hi_done);
    m_hi.hi_error_out(m_hi_err);    m_irq.hi_error_in(m_hi_err);

    // Flash Interface (FI) -> IRQ Controller
    m_fi.fi_done_out(m_fi_done);    m_irq.fi_done_in(m_fi_done);
    m_fi.fi_error_out(m_fi_err);    m_irq.fi_error_in(m_fi_err);

    // Command Processor (CP) -> IRQ Controller
    m_cp.cp_done_out(m_cp_done);    m_irq.cp_done_in(m_cp_done);
    m_cp.cp_error_out(m_cp_err);    m_irq.cp_error_in(m_cp_err);

    // Host UART -> IRQ Controller & Protocol Engine
    m_uart.uart_rx_irq_out(m_ua_rx);
    m_irq.ua_rx_in(m_ua_rx);
    m_pe.uart_rx_irq_in(m_ua_rx);   // Dual binding for UART RX

    m_uart.uart_err_irq_out(m_ua_err);
    m_irq.ua_err_in(m_ua_err);

    // Protocol Engine (PE) -> IRQ Controller
    m_pe.pe_done_out(m_pe_done);    m_irq.pe_done_in(m_pe_done);
    m_pe.pe_error_out(m_pe_err);    m_irq.pe_error_in(m_pe_err);

    // =================================================================
    // TODO 4: the Interrupt Controller's level output
    // =================================================================
    m_irq.irq_out(irq_out);
}
