// SPDX-License-Identifier: GPL-3.0-only
// Framework doubles for extracted BSD-3-Clause MAME SID socket methods.
// This models calls and route gains, not SID analog/audio synthesis.
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
using offs_t = uint32_t;
#define BIT(value, bit) (((value) >> (bit)) & 1U)
struct sid_double {
    uint8_t id;
    unsigned reads = 0, writes = 0, address = 0;
    uint8_t data = 0;
    std::array<float, 2> gains{};
    uint8_t read(offs_t reg) { ++reads; address = reg; return uint8_t(id * 32 + reg); }
    void write(offs_t reg, uint8_t value) { ++writes; address = reg; data = value; }
    void set_route_gain(unsigned, void *, unsigned channel, float gain) { gains.at(channel) = gain; }
};
struct config_double { uint8_t value = 0; uint8_t read() { return value; } };
class c64_sid_6581_device {
public:
    sid_double *m_sid;
    uint8_t read(offs_t);
    void write(offs_t, uint8_t);
};
class c64_sid_dual6581_device {
public:
    sid_double *m_sid1, *m_sid2;
    config_double *m_config;
    void *m_slot = nullptr;
    bool m_a8 = true;
    uint8_t read(offs_t);
    void write(offs_t, uint8_t);
    void device_reset();
#include "mame_sid_select.inc"
};
#include "mame_sid_methods.inc"

int main(int argc, char **argv) {
    try {
        if (argc != 2) throw std::runtime_error("Expected output path");
        std::ofstream out(argv[1]);
        if (!out) throw std::runtime_error("Cannot open output");
        unsigned count = 0;
        for (unsigned mode : {0U, 2U, 3U}) {
            sid_double left{0}, right{1};
            config_double config{uint8_t(2U | (mode == 3U))};
            c64_sid_6581_device single{&left};
            c64_sid_dual6581_device dual{&left, &right, &config};
            dual.device_reset();
            if (left.gains != std::array<float,2>{1.0f, 0.0f} ||
                right.gains != std::array<float,2>{0.0f, 1.0f})
                throw std::runtime_error("Pinned MAME stereo route changed");
            for (unsigned enabled : {0U, 1U}) for (unsigned address = 0xd400; address < 0xd800; ++address) {
                left.reads = right.reads = left.writes = right.writes = 0;
                uint8_t result = 0;
                if (enabled) {
                    if (mode == 0) { single.write(address, 0x5a); result = single.read(address); }
                    else { dual.write(address, 0x5a); result = dual.read(address); }
                }
                unsigned selected = enabled ? (right.writes ? 1 : 0) : 2;
                if (enabled) {
                    const auto &device = selected ? right : left;
                    if (left.writes + right.writes != 1 || left.reads + right.reads != 1 ||
                        device.address != (address & 31U) || device.data != 0x5a ||
                        result != selected * 32U + (address & 31U))
                        throw std::runtime_error("MAME socket did not route exactly one read/write");
                }
                out << std::hex << mode << ' ' << enabled << ' ' << address << ' ' << selected << '\n';
                ++count;
            }
        }
        out.flush();
        if (!out) throw std::runtime_error("Output write failed");
        std::cout << "PASS: MAME SID socket vectors " << count << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
