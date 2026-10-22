// SPDX-License-Identifier: GPL-3.0-only
// Test-only framework doubles for the verbatim pinned MAME VIC bus scheduler.
// The generated include supplies constants, declarations and method bodies.
// Rendering, interrupt callbacks and memory values are outside this comparison.
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#define BIT(v, n) (((v) >> (n)) & 1)
using offs_t = uint32_t;
struct attotime { static constexpr int zero = 0; };
struct timer_double { void adjust(int, int) {} };
struct memory_double { uint8_t read_byte(offs_t) { return 0; } };
struct bitmap_rgb32 {};
#include "vic_generated.inc"

int main(int argc, char **argv) {
    if (argc != 4) throw std::runtime_error("usage: vic_bus_oracle ntsc scenario trace.csv");
    const bool ntsc = std::stoi(argv[1]) != 0;
    const int scenario = std::stoi(argv[2]);
    mos6566_device vic(ntsc);
    vic.device_reset();
    vic.m_lp = 1;
    const int scroll = scenario == 2 ? 3 : scenario == 3 ? 7 : 0;
    vic.m_reg[0x11] = scenario == 0 || scenario == 4 || scenario == 5 ? 0 : 0x18 | scroll;
    vic.m_reg[0x15] = scenario < 4 ? 0 : scenario == 4 ? 1 : scenario == 5 ? 0x80 : 0xff;
    vic.m_reg[0x17] = scenario == 8 ? 0xff : scenario == 9 ? 0x55 : 0;
    for (int i = 0; i < 8; ++i)
        vic.m_reg[1 + 2 * i] = scenario == 7 ? 72 + 3*i : scenario == 10 ? 255 : scenario == 11 ? 0 : 80;
    // Register values are static before a warm-up frame; register-write edge
    // behavior is deliberately outside this bus-ownership fixture.
    const int cycles = ntsc ? 65 : 63, lines = ntsc ? 263 : 312;
    std::ofstream trace(argv[3]);
    trace.exceptions(std::ios::failbit | std::ios::badbit);
    trace << "line,cycle,raster,ba,aec,dma,badline\n";
    bool started = false;
    int anchors = 0, count = 0;
    for (int step = 0; step < 4*cycles*lines; ++step) {
        const int cycle = vic.m_cycle;
        vic.m_icount = 1;
        vic.execute_run();
        if (vic.m_beam_line == 0 && cycle == 11) {
            ++anchors;
            if (anchors == 2) started = true;
        }
        if (started) {
            trace << vic.m_beam_line << ',' << cycle << ',' << vic.m_rasterline << ','
                  << vic.m_ba << ',' << vic.m_aec << ',' << int(vic.m_spr_dma) << ','
                  << vic.m_badline << '\n';
            if (++count == cycles*lines) break;
        }
    }
    if (count != cycles*lines) throw std::runtime_error("incomplete reference frame");
    trace.close();
    std::cout << "PASS reference VIC ownership cycles=" << count << '\n';
}
