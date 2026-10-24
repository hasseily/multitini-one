// SPDX-License-Identifier: GPL-3.0-only
// Draft for M0. Port edges are related; never reset guest memory contents.
`timescale 1ns/1ps
module multitini_c64_island_memory (
    input wire clk_cpu, clk_native,
    input wire cpu_ce, cpu_ram_we, cpu_colour_we,
    input wire [15:0] cpu_addr,
    input wire [7:0] cpu_wdata,
    output reg [7:0] ram_low_q = 0, ram_high_q = 0,
    output reg [7:0] rom_q = 0, char_q = 0,
    output reg [3:0] colour_q = 0,
    input wire loader_we,
    input wire [15:0] loader_addr,
    input wire [7:0] loader_data,
    input wire machine_ce, machine_we, machine_store,
    input wire [15:0] machine_addr,
    input wire [7:0] machine_data,
    output wire [7:0] machine_q,
    input wire peek_request, machine_paused,
    input wire [15:0] peek_addr,
    output reg peek_valid = 0,
    output wire [7:0] peek_data,
    input wire [9:0] native_colour_addr,
    input wire native_colour_we,
    input wire [3:0] native_colour_data,
    output reg [3:0] native_colour_q = 0,
    input wire [13:0] rom_load_addr,
    input wire [7:0] rom_load_data,
    input wire rom_load_basic_kernal, rom_load_char
);
    // Separate 32 KiB arrays avoid the 64 KiB RAMB36 cascade. A15 affects
    // WE and the registered output select, not the common port-A read EN.
    (* ram_style="block" *) reg [7:0] main_low [0:32767];
    (* ram_style="block" *) reg [7:0] main_high [0:32767];
    (* ram_style="block" *) reg [7:0] basic_kernal [0:16383];
    (* ram_style="block" *) reg [7:0] character [0:4095];
    (* ram_style="block" *) reg [3:0] colour [0:1023];
    reg [7:0] native_low_q = 0, native_high_q = 0;
    reg native_bank_q = 0;
    reg write_response = 0;
    reg [7:0] write_q = 0;
    reg held_response = 0;
    reg [7:0] held_q = 0;
    wire do_peek = peek_request && machine_paused && !loader_we;
    wire native_ce = loader_we || do_peek || machine_ce;
    wire native_we = loader_we ||
        (!do_peek && machine_ce && machine_we && machine_store);
    wire [15:0] native_addr = loader_we ? loader_addr :
                                    do_peek ? peek_addr : machine_addr;
    wire [7:0] native_data = loader_we ? loader_data : machine_data;
    wire [7:0] native_q = native_bank_q ? native_high_q : native_low_q;
    wire cpu_low_we=cpu_ram_we && !cpu_addr[15];
    wire cpu_high_we=cpu_ram_we && cpu_addr[15];
    wire native_low_ce=native_ce && !native_addr[15];
    wire native_high_ce=native_ce && native_addr[15];
    assign machine_q = held_response ? held_q : write_response ? write_q : native_q;
    assign peek_data = native_q;

    // NO_CHANGE on a port-A write. The unused bank may harmlessly read.
    always @(negedge clk_cpu) begin
        if (cpu_ce) begin
            if (cpu_low_we) main_low[cpu_addr[14:0]] <= cpu_wdata;
            else ram_low_q <= main_low[cpu_addr[14:0]];
        end
    end
    always @(negedge clk_cpu) begin
        if (cpu_ce) begin
            if (cpu_high_we) main_high[cpu_addr[14:0]] <= cpu_wdata;
            else ram_high_q <= main_high[cpu_addr[14:0]];
        end
    end
    always @(negedge clk_cpu) begin
        if (cpu_ce) begin
            rom_q <= basic_kernal[{cpu_addr[14],cpu_addr[12:0]}];
            char_q <= character[cpu_addr[11:0]];
        end
    end
    // Colour goes through the island's additional IO2 input register. Read
    // ahead even between grants so its two-slot read gap works at sparse
    // governor rates too. Only an accepted, qualified store changes memory.
    always @(negedge clk_cpu) begin
        if (cpu_ce && cpu_colour_we) colour[cpu_addr[9:0]] <= cpu_wdata[3:0];
        else colour_q <= colour[cpu_addr[9:0]];
    end
    always @(posedge clk_native) begin
        if (native_low_ce) begin
            if (native_we) begin
                main_low[native_addr[14:0]] <= native_data;
            end else native_low_q <= main_low[native_addr[14:0]];
        end
    end
    always @(posedge clk_native) begin
        if (native_high_ce) begin
            if (native_we) begin
                main_high[native_addr[14:0]] <= native_data;
            end else native_high_q <= main_high[native_addr[14:0]];
        end
    end
    // Both primitive ports are NO_CHANGE. The registered write echo provides
    // native WRITE_FIRST semantics (also for mapper-blocked stores), without
    // the READ_FIRST timing mode or an unsupported deep-TDP inference form.
    // Loader priority preserves the old machine response, including echo.
    always @(posedge clk_native) begin
        if (loader_we || do_peek) begin
            if (!held_response) held_q <= machine_q;
            held_response <= 1;
        end else if (machine_ce) held_response <= 0;
        if (native_ce) begin
            native_bank_q <= native_addr[15];
        end
        if (!loader_we && !do_peek && machine_ce) begin
            write_response <= machine_we;
            if (machine_we) write_q <= machine_data;
        end
        peek_valid <= do_peek;
        if (native_colour_we) begin
            colour[native_colour_addr] <= native_colour_data;
            native_colour_q <= native_colour_data;
        end else native_colour_q <= colour[native_colour_addr];
        if (rom_load_basic_kernal) basic_kernal[rom_load_addr] <= rom_load_data;
        if (rom_load_char) character[rom_load_addr[11:0]] <= rom_load_data;
    end
endmodule
