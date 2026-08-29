################################################################################
# Appletini One Pin Constraints
#
# Xilinx Zynq-7020 CLG484 Package
# Apple //e Expansion Card - Multi-Card Emulator
#
# Generated from: Xilinx_Zynq-7020_FormalTruthTable.csv
# Device: xc7z020clg484-1
#
# Note: PS pins (MIO, DDR) are configured by the Zynq PS IP and should not
# be constrained here. Only PL (Programmable Logic) pins are constrained.
################################################################################

################################################################################
# Apple //e Bus Interface - Bank 33
# All signals interface via level translators (bidirectional) or open-collector
################################################################################

## Apple //e Control Signals - Assert (Output from FPGA)
set_property PACKAGE_PIN U19 [get_ports a2ctrl_irq_n]
set_property IOSTANDARD LVCMOS33 [get_ports a2ctrl_irq_n]

set_property PACKAGE_PIN T21 [get_ports a2ctrl_nmi_n]
set_property IOSTANDARD LVCMOS33 [get_ports a2ctrl_nmi_n]

set_property PACKAGE_PIN U21 [get_ports a2ctrl_rdy_n]
set_property IOSTANDARD LVCMOS33 [get_ports a2ctrl_rdy_n]

set_property PACKAGE_PIN T22 [get_ports a2ctrl_dma_n]
set_property IOSTANDARD LVCMOS33 [get_ports a2ctrl_dma_n]

set_property PACKAGE_PIN W22 [get_ports a2ctrl_inh_n]
set_property IOSTANDARD LVCMOS33 [get_ports a2ctrl_inh_n]

set_property PACKAGE_PIN W20 [get_ports a2ctrl_reset_n]
set_property IOSTANDARD LVCMOS33 [get_ports a2ctrl_reset_n]

## Apple //e Control Signals - Observe (Input to FPGA)
set_property PACKAGE_PIN U22 [get_ports a2fpga_dma_n]
set_property -dict {IOSTANDARD LVCMOS33 PULLTYPE PULLUP} \
    [get_ports a2fpga_dma_n]

set_property PACKAGE_PIN V22 [get_ports a2fpga_rdy_n]
set_property -dict {IOSTANDARD LVCMOS33 PULLTYPE PULLUP} \
    [get_ports a2fpga_rdy_n]

set_property PACKAGE_PIN W21 [get_ports a2fpga_inh_n]
set_property -dict {IOSTANDARD LVCMOS33 PULLTYPE PULLUP} \
    [get_ports a2fpga_inh_n]

set_property PACKAGE_PIN U20 [get_ports a2fpga_nmi_n]
set_property -dict {IOSTANDARD LVCMOS33 PULLTYPE PULLUP} \
    [get_ports a2fpga_nmi_n]

set_property PACKAGE_PIN V20 [get_ports a2fpga_reset_n]
set_property -dict {IOSTANDARD LVCMOS33 PULLTYPE PULLUP} \
    [get_ports a2fpga_reset_n]

set_property PACKAGE_PIN V19 [get_ports a2fpga_irq_n]
set_property -dict {IOSTANDARD LVCMOS33 DRIVE 16 SLEW FAST PULLTYPE PULLUP} \
    [get_ports a2fpga_irq_n]

## Apple //e Bus Transceiver Control
set_property PACKAGE_PIN V18 [get_ports a2fpga_oe_n]
set_property IOSTANDARD LVCMOS33 [get_ports a2fpga_oe_n]

set_property PACKAGE_PIN Y20 [get_ports a2fpga_oe_n_aux]
set_property IOSTANDARD LVCMOS33 [get_ports a2fpga_oe_n_aux]

set_property PACKAGE_PIN U17 [get_ports a2fpga_dir_a]
set_property IOSTANDARD LVCMOS33 [get_ports a2fpga_dir_a]

set_property PACKAGE_PIN V17 [get_ports a2fpga_dir_d]
# DIR_D closes the data-drive window from raw PHI0. Keep its current 12 mA
# drive and use the fast output edge to cut the pad delay on that release.
set_property -dict {IOSTANDARD LVCMOS33 DRIVE 12 SLEW FAST} \
    [get_ports a2fpga_dir_d]

## Apple //e Address Bus (Bidirectional)
set_property PACKAGE_PIN AA13 [get_ports {a2fpga_a[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[0]}]

set_property PACKAGE_PIN AB14 [get_ports {a2fpga_a[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[1]}]

set_property PACKAGE_PIN AA14 [get_ports {a2fpga_a[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[2]}]

set_property PACKAGE_PIN AB15 [get_ports {a2fpga_a[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[3]}]

set_property PACKAGE_PIN AB16 [get_ports {a2fpga_a[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[4]}]

set_property PACKAGE_PIN AA16 [get_ports {a2fpga_a[5]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[5]}]

set_property PACKAGE_PIN Y16 [get_ports {a2fpga_a[6]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[6]}]

set_property PACKAGE_PIN Y15 [get_ports {a2fpga_a[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[7]}]

set_property PACKAGE_PIN Y19 [get_ports {a2fpga_a[8]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[8]}]

set_property PACKAGE_PIN AB19 [get_ports {a2fpga_a[9]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[9]}]

set_property PACKAGE_PIN AA19 [get_ports {a2fpga_a[10]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[10]}]

set_property PACKAGE_PIN AB20 [get_ports {a2fpga_a[11]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[11]}]

set_property PACKAGE_PIN AB21 [get_ports {a2fpga_a[12]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[12]}]

set_property PACKAGE_PIN AB22 [get_ports {a2fpga_a[13]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[13]}]

set_property PACKAGE_PIN AA21 [get_ports {a2fpga_a[14]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[14]}]

set_property PACKAGE_PIN AA22 [get_ports {a2fpga_a[15]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_a[15]}]

## Apple //e Data Bus (Bidirectional)
set_property PACKAGE_PIN Y14 [get_ports {a2fpga_d[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_d[0]}]

set_property PACKAGE_PIN W15 [get_ports {a2fpga_d[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_d[1]}]

set_property PACKAGE_PIN W16 [get_ports {a2fpga_d[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_d[2]}]

set_property PACKAGE_PIN V15 [get_ports {a2fpga_d[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_d[3]}]

set_property PACKAGE_PIN V14 [get_ports {a2fpga_d[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_d[4]}]

set_property PACKAGE_PIN U16 [get_ports {a2fpga_d[5]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_d[5]}]

set_property PACKAGE_PIN U15 [get_ports {a2fpga_d[6]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_d[6]}]

set_property PACKAGE_PIN U14 [get_ports {a2fpga_d[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {a2fpga_d[7]}]

## Apple //e Bus Control
set_property PACKAGE_PIN Y21 [get_ports a2fpga_rdwr_n]
set_property IOSTANDARD LVCMOS33 [get_ports a2fpga_rdwr_n]

## Apple //e Clock Inputs
set_property PACKAGE_PIN Y18 [get_ports a2fpga_clk]
set_property -dict {IOSTANDARD LVCMOS33 PULLTYPE PULLDOWN} \
    [get_ports a2fpga_clk]

set_property PACKAGE_PIN AA18 [get_ports a2fpga_7m]
set_property -dict {IOSTANDARD LVCMOS33 PULLTYPE PULLDOWN} \
    [get_ports a2fpga_7m]

set_property PACKAGE_PIN W17 [get_ports a2fpga_q3]
set_property -dict {IOSTANDARD LVCMOS33 PULLTYPE PULLDOWN} \
    [get_ports a2fpga_q3]

## Apple //e Status/Select Signals
set_property PACKAGE_PIN W18 [get_ports a2fpga_m2b0]
set_property -dict {IOSTANDARD LVCMOS33 PULLTYPE PULLDOWN} \
    [get_ports a2fpga_m2b0]

set_property PACKAGE_PIN AA17 [get_ports a2fpga_m2sel]
set_property -dict {IOSTANDARD LVCMOS33 PULLTYPE PULLDOWN} \
    [get_ports a2fpga_m2sel]

set_property PACKAGE_PIN AB17 [get_ports a2fpga_devsel_n]
set_property -dict {IOSTANDARD LVCMOS33 PULLTYPE PULLUP} \
    [get_ports a2fpga_devsel_n]

################################################################################
# DVI Video Output - Bank 34
# Parallel video to TFP410 DVI transmitter
################################################################################

## DVI Clock and Control
set_property PACKAGE_PIN L21 [get_ports dvi_clk]
set_property IOSTANDARD LVCMOS33 [get_ports dvi_clk]

set_property PACKAGE_PIN L22 [get_ports dvi_de]
set_property IOSTANDARD LVCMOS33 [get_ports dvi_de]

set_property PACKAGE_PIN R19 [get_ports dvi_hsync]
set_property IOSTANDARD LVCMOS33 [get_ports dvi_hsync]

set_property PACKAGE_PIN T19 [get_ports dvi_vsync]
set_property IOSTANDARD LVCMOS33 [get_ports dvi_vsync]

## DVI Red Channel (5-bit)
set_property PACKAGE_PIN L18 [get_ports {dvi_red[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_red[0]}]

set_property PACKAGE_PIN K18 [get_ports {dvi_red[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_red[1]}]

set_property PACKAGE_PIN K20 [get_ports {dvi_red[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_red[2]}]

set_property PACKAGE_PIN K19 [get_ports {dvi_red[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_red[3]}]

set_property PACKAGE_PIN L19 [get_ports {dvi_red[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_red[4]}]

## DVI Green Channel (6-bit)
set_property PACKAGE_PIN P16 [get_ports {dvi_grn[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_grn[0]}]

set_property PACKAGE_PIN N17 [get_ports {dvi_grn[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_grn[1]}]

set_property PACKAGE_PIN R16 [get_ports {dvi_grn[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_grn[2]}]

set_property PACKAGE_PIN R18 [get_ports {dvi_grn[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_grn[3]}]

set_property PACKAGE_PIN T17 [get_ports {dvi_grn[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_grn[4]}]

set_property PACKAGE_PIN T18 [get_ports {dvi_grn[5]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_grn[5]}]

## DVI Blue Channel (5-bit)
set_property PACKAGE_PIN P18 [get_ports {dvi_blu[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_blu[0]}]

set_property PACKAGE_PIN T16 [get_ports {dvi_blu[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_blu[1]}]

set_property PACKAGE_PIN P17 [get_ports {dvi_blu[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_blu[2]}]

set_property PACKAGE_PIN P15 [get_ports {dvi_blu[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_blu[3]}]

set_property PACKAGE_PIN N15 [get_ports {dvi_blu[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dvi_blu[4]}]

## DVI I2C (DDC/EDID)
set_property PACKAGE_PIN L17 [get_ports dvi_scl]
set_property IOSTANDARD LVCMOS33 [get_ports dvi_scl]

set_property PACKAGE_PIN M17 [get_ports dvi_sda]
set_property IOSTANDARD LVCMOS33 [get_ports dvi_sda]

## DVI Hot Plug Detect
set_property PACKAGE_PIN K21 [get_ports dvi_hpd]
set_property IOSTANDARD LVCMOS33 [get_ports dvi_hpd]

################################################################################
# Audio Output - Bank 34
################################################################################

## I2S Audio
set_property PACKAGE_PIN M19 [get_ports audio_i2s_mclk]
set_property IOSTANDARD LVCMOS33 [get_ports audio_i2s_mclk]

set_property PACKAGE_PIN J15 [get_ports audio_i2s_lrck]
set_property IOSTANDARD LVCMOS33 [get_ports audio_i2s_lrck]

set_property PACKAGE_PIN K15 [get_ports audio_i2s_sdata]
set_property IOSTANDARD LVCMOS33 [get_ports audio_i2s_sdata]

set_property PACKAGE_PIN J16 [get_ports audio_i2s_bick]
set_property IOSTANDARD LVCMOS33 [get_ports audio_i2s_bick]

## SPDIF Audio
set_property PACKAGE_PIN J17 [get_ports audio_spdif_out]
set_property IOSTANDARD LVCMOS33 [get_ports audio_spdif_out]

################################################################################
# PSRAM - Bank 34
# Two 4-bit wide PSRAM chips (A and B) with shared clock and chip enable
################################################################################

## PSRAM Chip A Data
set_property -dict \
    {package_pin P21 IOSTANDARD LVCMOS33 PULLTYPE {} SLEW FAST DRIVE 8} [get_ports {psram_a_io[0]}]
set_property -dict \
    {package_pin N20 IOSTANDARD LVCMOS33 PULLTYPE {} SLEW FAST DRIVE 8} [get_ports {psram_a_io[1]}]
set_property -dict \
    {package_pin N19 IOSTANDARD LVCMOS33 PULLTYPE {} SLEW FAST DRIVE 8} [get_ports {psram_a_io[2]}]
set_property -dict \
    {package_pin N22 IOSTANDARD LVCMOS33 PULLTYPE {} SLEW FAST DRIVE 8} [get_ports {psram_a_io[3]}]

set_property -dict \
    {package_pin R21 IOSTANDARD LVCMOS33 PULLTYPE {} SLEW FAST DRIVE 8} [get_ports {psram_b_io[0]}]
set_property -dict \
    {package_pin P20 IOSTANDARD LVCMOS33 PULLTYPE {} SLEW FAST DRIVE 8} [get_ports {psram_b_io[1]}]
set_property -dict \
    {package_pin R20 IOSTANDARD LVCMOS33 PULLTYPE {} SLEW FAST DRIVE 8} [get_ports {psram_b_io[2]}]
set_property -dict \
    {package_pin P22 IOSTANDARD LVCMOS33 PULLTYPE {} SLEW FAST DRIVE 8} [get_ports {psram_b_io[3]}]

set_property -dict \
    {package_pin M21 IOSTANDARD LVCMOS33 PULLTYPE {} SLEW FAST DRIVE 8} [get_ports {psram_ce_n}]

set_property -dict \
    {package_pin M22 IOSTANDARD LVCMOS33 PULLTYPE {} SLEW FAST DRIVE 8} [get_ports {psram_clk}]

# set_property PACKAGE_PIN P21 [get_ports {psram_a_io[0]}]
# set_property IOSTANDARD LVCMOS33 [get_ports {psram_a_io[0]}]

# set_property PACKAGE_PIN N20 [get_ports {psram_a_io[1]}]
# set_property IOSTANDARD LVCMOS33 [get_ports {psram_a_io[1]}]

# set_property PACKAGE_PIN N19 [get_ports {psram_a_io[2]}]
# set_property IOSTANDARD LVCMOS33 [get_ports {psram_a_io[2]}]

# set_property PACKAGE_PIN N22 [get_ports {psram_a_io[3]}]
# set_property IOSTANDARD LVCMOS33 [get_ports {psram_a_io[3]}]

# ## PSRAM Chip B Data
# set_property PACKAGE_PIN R21 [get_ports {psram_b_io[0]}]
# set_property IOSTANDARD LVCMOS33 [get_ports {psram_b_io[0]}]

# set_property PACKAGE_PIN P20 [get_ports {psram_b_io[1]}]
# set_property IOSTANDARD LVCMOS33 [get_ports {psram_b_io[1]}]

# set_property PACKAGE_PIN R20 [get_ports {psram_b_io[2]}]
# set_property IOSTANDARD LVCMOS33 [get_ports {psram_b_io[2]}]

# set_property PACKAGE_PIN P22 [get_ports {psram_b_io[3]}]
# set_property IOSTANDARD LVCMOS33 [get_ports {psram_b_io[3]}]

# ## PSRAM Control (Shared)
# set_property PACKAGE_PIN M21 [get_ports psram_ce_n]
# set_property IOSTANDARD LVCMOS33 [get_ports psram_ce_n]

# set_property PACKAGE_PIN M22 [get_ports psram_clk]
# set_property IOSTANDARD LVCMOS33 [get_ports psram_clk]

################################################################################
# Peripheral Interfaces - Bank 34
################################################################################

## RTC (Real-Time Clock)
set_property PACKAGE_PIN J21 [get_ports i2c0_rtc_int_n]
set_property IOSTANDARD LVCMOS33 [get_ports i2c0_rtc_int_n]

set_property PACKAGE_PIN J22 [get_ports i2c0_rtc_clkout]
set_property IOSTANDARD LVCMOS33 [get_ports i2c0_rtc_clkout]

## Temperature Sensor
set_property PACKAGE_PIN J20 [get_ports i2c0_temp_alert_n]
set_property IOSTANDARD LVCMOS33 [get_ports i2c0_temp_alert_n]

## Ethernet W5100S Parallel MCU Interface - Bank 35
set_property PACKAGE_PIN B19 [get_ports {eth_d[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {eth_d[1]}]
set_property PACKAGE_PIN B20 [get_ports {eth_d[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {eth_d[3]}]
set_property PACKAGE_PIN A21 [get_ports {eth_d[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {eth_d[4]}]
set_property PACKAGE_PIN A22 [get_ports {eth_d[6]}]
set_property IOSTANDARD LVCMOS33 [get_ports {eth_d[6]}]
set_property PACKAGE_PIN B21 [get_ports {eth_d[5]}]
set_property IOSTANDARD LVCMOS33 [get_ports {eth_d[5]}]
set_property PACKAGE_PIN B22 [get_ports {eth_d[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {eth_d[7]}]
set_property PACKAGE_PIN A18 [get_ports {eth_d[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {eth_d[0]}]
set_property PACKAGE_PIN A19 [get_ports {eth_d[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {eth_d[2]}]

set_property PACKAGE_PIN A16 [get_ports {eth_a[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {eth_a[0]}]
set_property PACKAGE_PIN B16 [get_ports {eth_a[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {eth_a[1]}]
set_property PACKAGE_PIN A17 [get_ports eth_rd_n]
set_property IOSTANDARD LVCMOS33 [get_ports eth_rd_n]
set_property PACKAGE_PIN B17 [get_ports eth_wr_n]
set_property IOSTANDARD LVCMOS33 [get_ports eth_wr_n]
set_property PACKAGE_PIN B15 [get_ports eth_cs_n]
set_property IOSTANDARD LVCMOS33 [get_ports eth_cs_n]
set_property PACKAGE_PIN C22 [get_ports eth_rst_n]
set_property IOSTANDARD LVCMOS33 [get_ports eth_rst_n]
set_property PACKAGE_PIN C20 [get_ports eth_int_n]
set_property IOSTANDARD LVCMOS33 [get_ports eth_int_n]

## USB System Fault
set_property PACKAGE_PIN H15 [get_ports usb_sys_fault_n]
set_property IOSTANDARD LVCMOS33 [get_ports usb_sys_fault_n]

################################################################################
# Clock Constraints
