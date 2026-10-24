-- SPDX-License-Identifier: GPL-3.0-only
-- Observation/record adapter only. The pinned command_protocol is unchanged.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_pkg.all;
use work.slot_bus_pkg.all;

entity c64_uci_upstream_bridge is
    port (
        clk, reset : in std_logic;
        bus_valid, bus_write : in std_logic;
        bus_addr : in std_logic_vector(2 downto 0);
        bus_wdata : in std_logic_vector(7 downto 0);
        bus_rdata : out std_logic_vector(7 downto 0);
        bus_selected, irq, freeze, host_irq : out std_logic;
        ff00_write : in std_logic;
        host_read, host_write : in std_logic;
        host_addr : in std_logic_vector(3 downto 0);
        host_wdata : in std_logic_vector(7 downto 0);
        host_rdata : out std_logic_vector(7 downto 0);
        host_ack : out std_logic;
        ram_addr : out std_logic_vector(10 downto 0);
        ram_rdata : in std_logic_vector(7 downto 0);
        ram_wdata : out std_logic_vector(7 downto 0);
        ram_ce, ram_we : out std_logic
    );
end entity;

architecture records_only of c64_uci_upstream_bridge is
    signal io_req : t_io_req := c_io_req_init;
    signal io_resp : t_io_resp;
    signal slot_req : t_slot_req := c_slot_req_init;
    signal slot_resp : t_slot_resp;
    signal address : unsigned(10 downto 0);
begin
    io_req.read <= host_read;
    io_req.write <= host_write;
    io_req.address <= resize(unsigned(host_addr), 24);
    io_req.data <= host_wdata;
    host_rdata <= io_resp.data;
    host_ack <= io_resp.ack;
    slot_req.bus_address <= unsigned'(X"DF18") + resize(unsigned(bus_addr), 16);
    slot_req.io_address <= unsigned'(X"DF18") + resize(unsigned(bus_addr), 16);
    slot_req.bus_rwn <= not bus_write;
    slot_req.bus_write <= bus_valid and bus_write;
    slot_req.io_write <= bus_valid and bus_write;
    slot_req.io_read <= bus_valid and not bus_write;
    slot_req.data <= bus_wdata;
    bus_rdata <= slot_resp.data;
    bus_selected <= slot_resp.reg_output;
    irq <= slot_resp.irq;
    ram_addr <= std_logic_vector(address);
    reference: entity work.command_protocol
        port map (
            clock => clk, reset => reset, io_req => io_req, io_resp => io_resp,
            io_irq => host_irq, slot_req => slot_req, slot_resp => slot_resp,
            freeze => freeze, write_ff00 => ff00_write,
            address => address, rdata => ram_rdata, wdata => ram_wdata,
            en => ram_ce, we => ram_we
        );
end architecture;
