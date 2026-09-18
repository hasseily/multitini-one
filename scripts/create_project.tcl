# SPDX-License-Identifier: GPL-3.0-only
# Recreate the Multitini One Vivado 2025.2 project from tracked sources.

set script_dir [file dirname [file normalize [info script]]]
set repo_root [file normalize [file join $script_dir ".."]]
set project_dir [file join $repo_root "build" "vivado"]
if {[info exists ::env(MULTITINI_VIVADO_DIR)]} {
    set requested_dir [string trim $::env(MULTITINI_VIVADO_DIR)]
    if {$requested_dir eq ""} {
        error "MULTITINI_VIVADO_DIR must not be empty"
    }
    if {[file pathtype $requested_dir] eq "absolute"} {
        set project_dir [file normalize $requested_dir]
    } else {
        set project_dir [file normalize [file join $repo_root $requested_dir]]
    }
}
set project_name "multitini_one"
set part_name "xc7z020clg484-2"

set machine "SUPERVISOR"
if {[info exists ::env(MULTITINI_MACHINE)]} {
    set machine [string toupper [string trim $::env(MULTITINI_MACHINE)]]
}
set c64_video_standard "PAL"
set c64_ntsc_mode 0
set active_machine 2
switch -- $machine {
    SUPERVISOR {
        # The boot image contains only the shared shell and standby UI. Keep a
        # valid machine-domain clock for the common CDC mailboxes, but do not
        # import or instantiate any machine core.
        set c64_clock_mhz 31.527954
        set c64_video_standard "N/A"
    }
    C64 {
        set active_machine 0
        if {[info exists ::env(MULTITINI_C64_VIDEO_STANDARD)]} {
            set c64_video_standard \
                [string toupper [string trim $::env(MULTITINI_C64_VIDEO_STANDARD)]]
        }
        switch -- $c64_video_standard {
            PAL {
                set c64_clock_mhz 31.527954
                set c64_ntsc_mode 0
            }
            NTSC {
                set c64_clock_mhz 32.727266
                set c64_ntsc_mode 1
            }
            default {
                error "MULTITINI_C64_VIDEO_STANDARD must be PAL or NTSC"
            }
        }
    }
    GBC {
        set c64_clock_mhz 33.554432
        set c64_video_standard "N/A"
        set active_machine 1
    }
    VECTREX {
        set c64_clock_mhz 96.000000
        set c64_video_standard "N/A"
        set active_machine 3
    }
    default {
        error "MULTITINI_MACHINE must be SUPERVISOR, C64, GBC, or VECTREX"
    }
}

set enable_gbc_psram 0
if {[info exists ::env(MULTITINI_ENABLE_GBC_PSRAM)]} {
    set requested [string trim $::env(MULTITINI_ENABLE_GBC_PSRAM)]
    if {$requested ni {0 1}} {
        error "MULTITINI_ENABLE_GBC_PSRAM must be 0 or 1"
    }
    set enable_gbc_psram $requested
}
if {$enable_gbc_psram && $machine ne "GBC"} {
    error "MULTITINI_ENABLE_GBC_PSRAM is valid only for GBC builds"
}

proc require_file {path} {
    if {![file isfile $path]} {
        error "Required source is absent: $path"
    }
    return [file normalize $path]
}

proc add_manifest_sources {repo_root machine} {
    set manifest [require_file [file join $repo_root hdl hdl_sources.txt]]
    set stream [open $manifest r]
    set lines [split [read -nonewline $stream] "\n"]
    close $stream

    set active_section "COMMON"
    foreach line $lines {
        set relative [string trim $line]
        if {$relative eq "" || [string index $relative 0] eq "#"} {
            continue
        }
        if {[regexp {^\[([A-Za-z0-9_]+)\]$} $relative -> section]} {
            set active_section [string toupper $section]
            if {$active_section ni {COMMON C64 GBC VECTREX}} {
                error "Unknown source-manifest section: $relative"
            }
            continue
        }
        if {$active_section ne "COMMON" && $active_section ne $machine} {
            continue
        }
        set fields [split $relative "|"]
        if {[llength $fields] > 2} {
            error "Malformed source-manifest entry: $relative"
        }
        set relative [string trim [lindex $fields 0]]
        set language ""
        if {[llength $fields] == 2} {
            set language [string toupper [string trim [lindex $fields 1]]]
            if {$language ni {SYSTEMVERILOG}} {
                error "Unsupported source-manifest language: $language"
            }
        }
        set source [require_file [file join $repo_root hdl $relative]]
        set extension [string tolower [file extension $source]]
        if {$extension eq ".xdc"} {
            add_files -fileset constrs_1 -norecurse $source
            if {[file tail $source] in {
                    "multitini_timing.xdc" "multitini_psram_timing.xdc"
                    "multitini_vectrex_psram_timing.xdc"}} {
                set_property USED_IN_SYNTHESIS false [get_files $source]
            }
        } else {
            add_files -fileset sources_1 -norecurse $source
            if {$extension eq ".sv" || $language eq "SYSTEMVERILOG"} {
                set_property file_type SystemVerilog [get_files $source]
            } elseif {$extension eq ".vhd" || $extension eq ".vhdl"} {
                set_property file_type {VHDL 2008} [get_files $source]
            } elseif {$extension ne ".v" && $extension ne ".mem" &&
                      $extension ne ".hex"} {
                error "Unsupported source-manifest extension: $source"
            }
        }
    }
}

proc configure_processing_system {ps} {
    # Clock and fabric interfaces.
    set_property -dict [list \
        CONFIG.PCW_FPGA_FCLK0_ENABLE {1} \
        CONFIG.PCW_FPGA_FCLK1_ENABLE {0} \
        CONFIG.PCW_FPGA_FCLK2_ENABLE {1} \
        CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ {133.000000} \
        CONFIG.PCW_FPGA2_PERIPHERAL_FREQMHZ {150.000000} \
        CONFIG.PCW_EN_CLK0_PORT {1} \
        CONFIG.PCW_EN_CLK1_PORT {0} \
        CONFIG.PCW_EN_CLK2_PORT {1} \
        CONFIG.PCW_FCLK0_PERIPHERAL_CLKSRC {IO PLL} \
        CONFIG.PCW_FCLK2_PERIPHERAL_CLKSRC {IO PLL} \
        CONFIG.PCW_FCLK_CLK0_BUF {TRUE} \
        CONFIG.PCW_FCLK_CLK2_BUF {TRUE} \
        CONFIG.PCW_EN_RST0_PORT {1} \
        CONFIG.PCW_USE_M_AXI_GP0 {1} \
        CONFIG.PCW_USE_S_AXI_HP0 {1} \
        CONFIG.PCW_USE_S_AXI_HP1 {1} \
        CONFIG.PCW_USE_FABRIC_INTERRUPT {1} \
        CONFIG.PCW_IRQ_F2P_INTR {1} \
        CONFIG.PCW_IRQ_F2P_MODE {DIRECT}] $ps

    set_property -dict [list \
        CONFIG.PCW_CRYSTAL_PERIPHERAL_FREQMHZ {33.333333} \
        CONFIG.PCW_APU_CLK_RATIO_ENABLE {6:2:1} \
        CONFIG.PCW_CPU_PERIPHERAL_CLKSRC {ARM PLL} \
        CONFIG.PCW_DDR_PERIPHERAL_CLKSRC {DDR PLL} \
        CONFIG.PCW_QSPI_PERIPHERAL_CLKSRC {IO PLL} \
        CONFIG.PCW_SDIO_PERIPHERAL_CLKSRC {IO PLL} \
        CONFIG.PCW_UART_PERIPHERAL_CLKSRC {IO PLL}] $ps

    # Two 16-bit DDR3L devices. These values are inherited from the pinned
    # Appletini board project and require confirmation in generated PS reports.
    set_property -dict [list \
        CONFIG.PCW_UIPARAM_DDR_PARTNO {MT41K256M16 RE-125} \
        CONFIG.PCW_UIPARAM_DDR_BUS_WIDTH {32 Bit} \
        CONFIG.PCW_UIPARAM_DDR_DEVICE_CAPACITY {4096 MBits} \
        CONFIG.PCW_UIPARAM_DDR_SPEED_BIN {DDR3L_800} \
        CONFIG.PCW_UIPARAM_DDR_TRAIN_WRITE_LEVEL {1} \
        CONFIG.PCW_UIPARAM_DDR_TRAIN_READ_GATE {1} \
        CONFIG.PCW_UIPARAM_DDR_TRAIN_DATA_EYE {1} \
        CONFIG.PCW_UIPARAM_DDR_DQS_TO_CLK_DELAY_0 {0.048} \
        CONFIG.PCW_UIPARAM_DDR_DQS_TO_CLK_DELAY_1 {0.050} \
        CONFIG.PCW_UIPARAM_DDR_DQS_TO_CLK_DELAY_2 {0.048} \
        CONFIG.PCW_UIPARAM_DDR_DQS_TO_CLK_DELAY_3 {0.050} \
        CONFIG.PCW_UIPARAM_DDR_BOARD_DELAY0 {0.294} \
        CONFIG.PCW_UIPARAM_DDR_BOARD_DELAY1 {0.298} \
        CONFIG.PCW_UIPARAM_DDR_BOARD_DELAY2 {0.294} \
        CONFIG.PCW_UIPARAM_DDR_BOARD_DELAY3 {0.298}] $ps

    # Boot flash and removable storage.
    set_property -dict [list \
        CONFIG.PCW_QSPI_PERIPHERAL_ENABLE {1} \
        CONFIG.PCW_QSPI_QSPI_IO {MIO 1 .. 6} \
        CONFIG.PCW_QSPI_GRP_FBCLK_ENABLE {0} \
        CONFIG.PCW_SD1_PERIPHERAL_ENABLE {1} \
        CONFIG.PCW_SD1_SD1_IO {MIO 10 .. 15} \
        CONFIG.PCW_SD1_GRP_CD_ENABLE {1} \
        CONFIG.PCW_SD1_GRP_CD_IO {MIO 9} \
        CONFIG.PCW_SD1_GRP_WP_ENABLE {0}] $ps

    foreach mio {1 2 3 4 5 6 9 10 11 12 13 14 15} {
        set_property "CONFIG.PCW_MIO_${mio}_SLEW" fast $ps
    }
    # Only MIO1 exposes a configurable pull-up in the PS7 IP. MIO2-6 are
    # fixed by the QSPI peripheral selection and reject explicit assignments.
    set_property CONFIG.PCW_MIO_1_PULLUP disabled $ps

    # Board-management bus and both debug UARTs.
    set_property -dict [list \
        CONFIG.PCW_I2C0_PERIPHERAL_ENABLE {1} \
        CONFIG.PCW_I2C0_I2C0_IO {MIO 22 .. 23} \
        CONFIG.PCW_I2C0_GRP_INT_ENABLE {0} \
        CONFIG.PCW_UART0_PERIPHERAL_ENABLE {1} \
        CONFIG.PCW_UART0_UART0_IO {MIO 26 .. 27} \
        CONFIG.PCW_UART0_GRP_FULL_ENABLE {0} \
        CONFIG.PCW_UART0_BAUD_RATE {921600} \
        CONFIG.PCW_UART1_PERIPHERAL_ENABLE {1} \
        CONFIG.PCW_UART1_UART1_IO {MIO 24 .. 25} \
        CONFIG.PCW_UART1_GRP_FULL_ENABLE {0} \
        CONFIG.PCW_UART1_BAUD_RATE {115200}] $ps

    # USB0 is the uplink/debug port. USB1 is the device-facing host port.
    # The schematic has no PS-MIO USB PHY reset signal, so both reset outputs
    # remain disabled pending the cold/warm-reset hardware check.
    set_property -dict [list \
        CONFIG.PCW_USB0_PERIPHERAL_ENABLE {1} \
        CONFIG.PCW_USB0_USB0_IO {MIO 28 .. 39} \
        CONFIG.PCW_USB0_RESET_ENABLE {0} \
        CONFIG.PCW_USB1_PERIPHERAL_ENABLE {1} \
        CONFIG.PCW_USB1_USB1_IO {MIO 40 .. 51} \
        CONFIG.PCW_USB1_RESET_ENABLE {0}] $ps
    foreach mio {28 29 30 31 32 33 34 35 36 37 38 39 \
                 40 41 42 43 44 45 46 47 48 49 50 51} {
        set_property "CONFIG.PCW_MIO_${mio}_SLEW" fast $ps
    }

    # Ethernet uses the PL-side W5100S, not a PS GEM.
    set_property -dict [list \
        CONFIG.PCW_EN_ENET0 {0} \
        CONFIG.PCW_EN_ENET1 {0} \
        CONFIG.PCW_ENET0_PERIPHERAL_ENABLE {0} \
        CONFIG.PCW_ENET1_PERIPHERAL_ENABLE {0} \
        CONFIG.PCW_ENET0_GRP_MDIO_ENABLE {0} \
        CONFIG.PCW_ENET1_GRP_MDIO_ENABLE {0} \
        CONFIG.PCW_GPIO_MIO_GPIO_ENABLE {1} \
        CONFIG.PCW_GPIO_EMIO_GPIO_ENABLE {0} \
        CONFIG.PCW_UIPARAM_GENERATE_SUMMARY {None}] $ps
}

file mkdir $project_dir
create_project $project_name $project_dir -part $part_name -force
set_property target_language Verilog [current_project]
set_property simulator_language Mixed [current_project]
set_property default_lib work [current_project]
add_manifest_sources $repo_root $machine
set psram_timing_file [require_file \
    [file join $repo_root hdl board multitini_psram_timing.xdc]]
if {$machine eq "VECTREX"} {
    # The Vectrex PHY has fixed falling-edge capture, covered by its own
    # input-delay constraints. Do not inherit the tunable PHY's false paths.
    set_property USED_IN_IMPLEMENTATION false [get_files $psram_timing_file]
}
if {$machine eq "SUPERVISOR" ||
    ($machine eq "GBC" && !$enable_gbc_psram)} {
    set psram_iob_file [require_file \
        [file join $repo_root hdl board multitini_psram_iob.xdc]]
    set_property USED_IN_SYNTHESIS false [get_files $psram_iob_file]
    set_property USED_IN_IMPLEMENTATION false \
        [get_files [list $psram_iob_file $psram_timing_file]]
}

create_bd_design zynq_ps_bd
set ps [create_bd_cell -type ip \
    -vlnv xilinx.com:ip:processing_system7:5.5 processing_system7_0]
apply_bd_automation -rule xilinx.com:bd_rule:processing_system7 -config {
    make_external "FIXED_IO, DDR"
    apply_board_preset "0"
    Master "Disable"
    Slave "Disable"
} $ps
configure_processing_system $ps

set gp0 [create_bd_intf_port -mode Master \
    -vlnv xilinx.com:interface:aximm_rtl:1.0 M_AXI_GP0_0]
set_property -dict [list \
    CONFIG.ADDR_WIDTH {32} \
    CONFIG.DATA_WIDTH {32} \
    CONFIG.HAS_REGION {0} \
    CONFIG.NUM_READ_OUTSTANDING {8} \
    CONFIG.NUM_WRITE_OUTSTANDING {8} \
    CONFIG.PROTOCOL {AXI3}] $gp0
connect_bd_intf_net [get_bd_intf_pins $ps/M_AXI_GP0] $gp0

# Video DMA uses independent 64-bit AXI3 read/write channels on HP0. The
# existing direct PS FCLK0 keeps every accepted DDR transaction drainable
# while firmware prepares a full-PL replacement.
set hp0 [create_bd_intf_port -mode Slave \
    -vlnv xilinx.com:interface:aximm_rtl:1.0 S_AXI_HP0_0]
set_property -dict [list CONFIG.ADDR_WIDTH {32} CONFIG.DATA_WIDTH {64} \
    CONFIG.ID_WIDTH {6} CONFIG.HAS_REGION {0} CONFIG.PROTOCOL {AXI3} \
    CONFIG.MAX_BURST_LENGTH {16} CONFIG.NUM_READ_OUTSTANDING {1} \
    CONFIG.NUM_WRITE_OUTSTANDING {1} CONFIG.READ_WRITE_MODE {READ_WRITE}] $hp0
connect_bd_intf_net $hp0 [get_bd_intf_pins $ps/S_AXI_HP0]

# The fixed-rate frame engine has its own HP port. Capture/scanout on HP0
# never wait behind a PL arbiter shared with history and spatial processing.
set hp1 [create_bd_intf_port -mode Slave \
    -vlnv xilinx.com:interface:aximm_rtl:1.0 S_AXI_HP1_0]
set_property -dict [list CONFIG.ADDR_WIDTH {32} CONFIG.DATA_WIDTH {64} \
    CONFIG.ID_WIDTH {6} CONFIG.HAS_REGION {0} CONFIG.PROTOCOL {AXI3} \
    CONFIG.MAX_BURST_LENGTH {16} CONFIG.NUM_READ_OUTSTANDING {1} \
    CONFIG.NUM_WRITE_OUTSTANDING {1} CONFIG.READ_WRITE_MODE {READ_WRITE}] $hp1
connect_bd_intf_net $hp1 [get_bd_intf_pins $ps/S_AXI_HP1]


set control_clk [create_bd_port -dir O -type clk control_clk]
set idelay_ref_clk [create_bd_port -dir O -type clk idelay_ref_clk]
set video_clk [create_bd_port -dir O -type clk video_clk]
set c64_clk [create_bd_port -dir O -type clk c64_clk]
set c64_capture_clk [create_bd_port -dir O -type clk c64_capture_clk]
set control_aresetn [create_bd_port -dir O -type rst control_aresetn]
set idelay_aresetn [create_bd_port -dir O -type rst idelay_aresetn]
set video_aresetn [create_bd_port -dir O -type rst video_aresetn]
set c64_aresetn [create_bd_port -dir O -type rst c64_aresetn]
set_property CONFIG.POLARITY ACTIVE_LOW $control_aresetn
set_property CONFIG.POLARITY ACTIVE_LOW $idelay_aresetn
set_property CONFIG.POLARITY ACTIVE_LOW $video_aresetn
set_property CONFIG.POLARITY ACTIVE_LOW $c64_aresetn
set_property -dict [list CONFIG.ASSOCIATED_BUSIF {M_AXI_GP0_0:S_AXI_HP0_0:S_AXI_HP1_0} \
    CONFIG.ASSOCIATED_RESET {control_aresetn}] $control_clk
set_property CONFIG.ASSOCIATED_RESET {video_aresetn} $video_clk
set_property CONFIG.ASSOCIATED_RESET {c64_aresetn} $c64_clk
set irq [create_bd_port -dir I IRQ]

set idelay_wiz [create_bd_cell -type ip \
    -vlnv xilinx.com:ip:clk_wiz:6.0 clk_wiz_idelay]
set_property -dict [list \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {199.995} \
    CONFIG.NUM_OUT_CLKS {1} \
    CONFIG.PRIM_SOURCE {No_buffer} \
    CONFIG.RESET_PORT {resetn} \
    CONFIG.RESET_TYPE {ACTIVE_LOW}] $idelay_wiz

set video_wiz [create_bd_cell -type ip \
    -vlnv xilinx.com:ip:clk_wiz:6.0 clk_wiz_video]
set_property -dict [list \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {148.5} \
    CONFIG.PRIM_IN_FREQ {150.000} \
    CONFIG.PRIM_SOURCE {No_buffer} \
    CONFIG.RESET_PORT {resetn} \
    CONFIG.RESET_TYPE {ACTIVE_LOW}] $video_wiz

# The requested frequency is selected above for the active machine. The cell
# and ports keep their historical c64 names to avoid unnecessary block-design
# churn. Vivado must report the achieved MMCM frequency and timing before use.
set c64_wiz [create_bd_cell -type ip \
    -vlnv xilinx.com:ip:clk_wiz:6.0 clk_wiz_c64]
set_property -dict [list \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ $c64_clock_mhz \
    CONFIG.PRIM_IN_FREQ {150.000} \
    CONFIG.PRIM_SOURCE {No_buffer} \
    CONFIG.RESET_PORT {resetn} \
    CONFIG.RESET_TYPE {ACTIVE_LOW}] $c64_wiz
if {$machine eq "VECTREX"} {
    # A phase-related input clock centers PSRAM sampling in its routed
    # round-trip data window. Only the eight input IOB registers use it.
    set_property -dict [list CONFIG.NUM_OUT_CLKS {2} \
        CONFIG.CLKOUT2_USED {true} \
        CONFIG.CLKOUT2_REQUESTED_OUT_FREQ $c64_clock_mhz \
        CONFIG.CLKOUT2_REQUESTED_PHASE {279.000}] $c64_wiz
}

set control_reset [create_bd_cell -type ip \
    -vlnv xilinx.com:ip:proc_sys_reset:5.0 reset_control]
set idelay_reset [create_bd_cell -type ip \
    -vlnv xilinx.com:ip:proc_sys_reset:5.0 reset_idelay]
set video_reset [create_bd_cell -type ip \
    -vlnv xilinx.com:ip:proc_sys_reset:5.0 reset_video]
set c64_reset [create_bd_cell -type ip \
    -vlnv xilinx.com:ip:proc_sys_reset:5.0 reset_c64]
set constant_one [create_bd_cell -type ip \
    -vlnv xilinx.com:ip:xlconstant:1.1 constant_one]
set_property -dict [list CONFIG.CONST_VAL {1} CONFIG.CONST_WIDTH {1}] \
    $constant_one

# Keep the PS GP0 AXI clock and its reset independent of every lock detector
# in the PL. Full-PL PCAP replaces the clock wizards themselves; making either
# half of the control domain wait on one can strand the first shell probe after
# configuration. FCLK0 and FCLK_RESET0_N remain sourced by the PS.
connect_bd_net [get_bd_pins $ps/FCLK_CLK0] \
    [get_bd_pins $idelay_wiz/clk_in1] $control_clk \
    [get_bd_pins $control_reset/slowest_sync_clk] \
    [get_bd_pins $ps/M_AXI_GP0_ACLK] [get_bd_pins $ps/S_AXI_HP0_ACLK] \
    [get_bd_pins $ps/S_AXI_HP1_ACLK]
connect_bd_net [get_bd_pins $ps/FCLK_CLK2] \
    [get_bd_pins $video_wiz/clk_in1] [get_bd_pins $c64_wiz/clk_in1]
connect_bd_net [get_bd_pins $ps/FCLK_RESET0_N] \
    [get_bd_pins $idelay_wiz/resetn] [get_bd_pins $video_wiz/resetn] \
    [get_bd_pins $c64_wiz/resetn] \
    [get_bd_pins $control_reset/ext_reset_in] \
    [get_bd_pins $idelay_reset/ext_reset_in] \
    [get_bd_pins $video_reset/ext_reset_in] \
    [get_bd_pins $c64_reset/ext_reset_in]

connect_bd_net [get_bd_pins $constant_one/dout] \
    [get_bd_pins $control_reset/dcm_locked]
connect_bd_net [get_bd_pins $control_reset/peripheral_aresetn] $control_aresetn

connect_bd_net [get_bd_pins $idelay_wiz/clk_out1] $idelay_ref_clk \
    [get_bd_pins $idelay_reset/slowest_sync_clk]
connect_bd_net [get_bd_pins $idelay_wiz/locked] \
    [get_bd_pins $idelay_reset/dcm_locked]
connect_bd_net [get_bd_pins $idelay_reset/peripheral_aresetn] $idelay_aresetn

connect_bd_net [get_bd_pins $video_wiz/clk_out1] $video_clk \
    [get_bd_pins $video_reset/slowest_sync_clk]
connect_bd_net [get_bd_pins $video_wiz/locked] \
    [get_bd_pins $video_reset/dcm_locked]
connect_bd_net [get_bd_pins $video_reset/peripheral_aresetn] $video_aresetn

connect_bd_net [get_bd_pins $c64_wiz/clk_out1] $c64_clk \
    [get_bd_pins $c64_reset/slowest_sync_clk]
if {$machine eq "VECTREX"} {
    connect_bd_net [get_bd_pins $c64_wiz/clk_out2] $c64_capture_clk
} else {
    connect_bd_net [get_bd_pins $c64_wiz/clk_out1] $c64_capture_clk
}
connect_bd_net [get_bd_pins $c64_wiz/locked] \
    [get_bd_pins $c64_reset/dcm_locked]
connect_bd_net [get_bd_pins $c64_reset/peripheral_aresetn] $c64_aresetn
connect_bd_net $irq [get_bd_pins $ps/IRQ_F2P]

assign_bd_address -offset 0x40000000 -range 0x00010000 \
    -target_address_space [get_bd_addr_spaces $ps/Data] \
    [get_bd_addr_segs M_AXI_GP0_0/Reg] -force

assign_bd_address -offset 0x00000000 -range 0x40000000 \
    -target_address_space [get_bd_addr_spaces S_AXI_HP0_0] \
    [get_bd_addr_segs $ps/S_AXI_HP0/HP0_DDR_LOWOCM] -force
assign_bd_address -offset 0x00000000 -range 0x40000000 \
    -target_address_space [get_bd_addr_spaces S_AXI_HP1_0] \
    [get_bd_addr_segs $ps/S_AXI_HP1/HP1_DDR_LOWOCM] -force

regenerate_bd_layout
validate_bd_design
save_bd_design
generate_target all [get_files zynq_ps_bd.bd]
set wrapper [make_wrapper -files [get_files zynq_ps_bd.bd] -top]
add_files -norecurse $wrapper

set_property top multitini_one_top [get_filesets sources_1]
set git_id "00000000"
if {![catch {exec git -C $repo_root rev-parse --short=8 HEAD} candidate]} {
    if {[regexp {^[0-9a-fA-F]{8}$} [string trim $candidate]]} {
        set git_id [string trim $candidate]
    }
}
if {[info exists ::env(MULTITINI_BUILD_ID)]} {
    set git_id [string trim $::env(MULTITINI_BUILD_ID)]
    if {![regexp {^[0-9a-fA-F]{8}$} $git_id]} {
        error "MULTITINI_BUILD_ID must contain exactly eight hexadecimal digits"
    }
}
set_property generic \
    "SHELL_BUILD_ID=32'h${git_id} C64_NTSC_MODE=1'b${c64_ntsc_mode} ACTIVE_MACHINE=${active_machine} ENABLE_GBC_PSRAM=1'b${enable_gbc_psram}" \
    [get_filesets sources_1]
update_compile_order -fileset sources_1
if {$machine eq "VECTREX"} {
    # Revisit the sparse residual routed paths after normal placement/routing.
    set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.IS_ENABLED true [get_runs impl_1]
    set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore [get_runs impl_1]
}

puts "Created $project_dir/$project_name.xpr"
puts "SHELL_BUILD_ID=0x$git_id"
puts "MACHINE=$machine"
puts "C64_VIDEO_STANDARD=$c64_video_standard"
puts "MACHINE_CLOCK_REQUEST_MHZ=$c64_clock_mhz"
puts "GBC_PSRAM_ENABLED=$enable_gbc_psram"
