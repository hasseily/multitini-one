// SPDX-License-Identifier: GPL-3.0-only
// Harness infrastructure only. The included controller methods are extracted
// verbatim from the pinned BSD-3-Clause MAME MOS8726 source.
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
using offs_t = uint32_t;
#define SUSPEND_REASON_DISABLE 0
#define LOGDMA(...) do {} while (0)
#define LOGREGS(...) do {} while (0)

struct machine_stub { bool side_effects_disabled() const { return false; } };
class mos8726_device {
public:
    std::ofstream trace;
    std::map<unsigned, uint8_t> c64, ram;
    int irq = 0, dma = 0;
    int m_icount = 0, m_bs = 1;
    bool m_ba = true;
    uint8_t m_status = 0, m_command = 0, m_imr = 0, m_acr = 0;
    uint16_t m_c64_addr = 0, m_c64_addr_shadow = 0, m_length = 0, m_length_shadow = 0;
    uint32_t m_reu_addr = 0, m_reu_addr_shadow = 0;
    bool m_active = false;
    machine_stub machine() const { return {}; }
    void suspend(int, bool) {} void resume(int) {}
    void m_write_irq(int value) { irq = value; }
    void m_write_dma(int value) { dma = value; }
    static uint8_t initial(unsigned addr, bool reu) {
        return uint8_t(addr ^ (addr >> 8) ^ (reu ? ((addr >> 16) ^ 0xa5) : 0x5a));
    }
    uint8_t access(bool reu, bool write, unsigned addr, uint8_t data = 0) {
        auto &mem = reu ? ram : c64;
        if (write) mem[addr] = data;
        else data = mem.count(addr) ? mem[addr] : initial(addr, reu);
        trace << (reu ? "M " : "C ") << (write ? "W " : "R ")
              << std::hex << std::setfill('0') << std::setw(6) << addr << ' '
              << std::setw(2) << unsigned(data) << '\n';
        return data;
    }
    uint8_t m_read_c64(unsigned addr) { return access(false, false, addr); }
    void m_write_c64(unsigned addr, uint8_t data) { access(false, true, addr, data); }
    uint8_t m_read_reu(unsigned addr) { return access(true, false, addr); }
    void m_write_reu(unsigned addr, uint8_t data) { access(true, true, addr, data); }
    void device_reset(); void execute_run();
    uint8_t reu_r(offs_t); void reu_w(offs_t, uint8_t);
    void transfer_byte(); void start_transfer(); void end_transfer(); void update_irq();
    uint8_t read(offs_t); void write(offs_t, uint8_t); void ff00_w();
};
#include "mame_reu_methods.inc"

int main(int argc, char **argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: oracle stimuli trace");
        std::ifstream input(argv[1]);
        mos8726_device chip; chip.trace.open(argv[2]);
        if (!input || !chip.trace) throw std::runtime_error("cannot open stream");
        unsigned op, a, b;
        while (input >> std::dec >> op >> std::hex >> a >> b) {
            switch (op) {
            case 0:
                chip.c64.clear(); chip.ram.clear(); chip.device_reset();
                chip.trace << "E " << std::dec << a << '\n'; break;
            case 1: chip.write(a, uint8_t(b)); break;
            case 2: {
                auto const data = chip.read(a);
                chip.trace << "R " << std::hex << std::setfill('0') << std::setw(2) << a
                           << ' ' << std::setw(2) << unsigned(data) << ' ' << chip.irq << '\n'; break;
            }
            case 3: chip.ff00_w(); break;
            case 4:
                chip.m_icount = 200000; chip.execute_run();
                if (chip.m_active) throw std::runtime_error("DMA did not finish");
                chip.trace << "S " << chip.irq << ' ' << chip.dma << '\n'; break;
            case 5: chip.c64[a] = uint8_t(b); break;
            case 6: chip.ram[a] = uint8_t(b); break;
            default: throw std::runtime_error("unknown stimulus");
            }
        }
        if (!input.eof()) throw std::runtime_error("malformed stimulus stream");
        chip.trace.flush();
        if (!chip.trace) throw std::runtime_error("trace write failed");
        std::cout << "PASS: pinned MAME REU oracle\n";
        return 0;
    } catch (std::exception const &error) { std::cerr << error.what() << '\n'; return 1; }
}
