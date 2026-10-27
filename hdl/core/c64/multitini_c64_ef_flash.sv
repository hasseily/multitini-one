// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// EasyFlash flash pair: ROML is chip 0 and ROMH chip 1, each an AMD
// AM29F040B in byte mode. Commands use the unlock cycles AA/55 at chip
// offsets 555/2AA, written in Ultimax mode as on the cartridge, where the
// C64 asserts ROML/ROMH for writes. Behaviour follows VICE 3.10
// flash040core.c (FLASH040_TYPE_B) for the commands EasyFlash software uses:
// autoselect (01h, A4h), byte program (AND into the stored byte), sector and
// chip erase, and reset. Reads of a busy chip return DQ7 data polling, a DQ6
// toggle on every read and DQ3 during erase. Operations complete as soon as
// cartridge memory is updated; real program and erase times are not modelled.
//
// Every accepted operation is queued for firmware before cartridge memory
// changes, so the firmware copy of the flash image can mirror it exactly.
// The queue holds back the next operation when full.
module multitini_c64_ef_flash #(
    parameter integer JOURNAL_DEPTH = 32
) (
    input  logic        clk,
    input  logic        reset,
    input  logic        enable,
    input  logic        ultimax,
    input  logic        access_ce,
    input  logic        access_we,
    input  logic        rom_l,
    input  logic        rom_h,
    input  logic [12:0] address,
    input  logic [7:0]  write_data,
    input  logic [5:0]  bank,
    output logic        read_override,
    output logic [7:0]  read_data,
    output logic        mem_valid,
    output logic        mem_write,
    output logic [23:0] mem_addr,
    output logic [7:0]  mem_wdata,
    input  logic        mem_accept,
    input  logic        mem_done,
    input  logic [7:0]  mem_rdata,
    output logic        erase_valid,
    output logic [2:0]  erase_group,
    output logic        erase_high,
    input  logic        erase_accept,
    input  logic        erase_done,
    output logic [30:0] journal_data,
    output logic        journal_valid,
    input  logic        journal_ready,
    output logic        busy
);
    localparam logic [2:0] CMD_READ = 3'd0, CMD_UNLOCK1 = 3'd1, CMD_UNLOCK2 = 3'd2,
        CMD_ERASE1 = 3'd3, CMD_ERASE2 = 3'd4, CMD_ERASE3 = 3'd5,
        CMD_PROGRAM = 3'd6, CMD_AUTOSELECT = 3'd7;
    localparam logic [1:0] OP_PROGRAM = 2'd1, OP_SECTOR_ERASE = 2'd2, OP_CHIP_ERASE = 2'd3;
    localparam logic [2:0] E_IDLE = 3'd0, E_JOURNAL = 3'd1, E_READ = 3'd2,
        E_READ_WAIT = 3'd3, E_WRITE = 3'd4, E_WRITE_WAIT = 3'd5,
        E_ERASE = 3'd6, E_ERASE_WAIT = 3'd7;
    localparam integer QUEUE_BITS = $clog2(JOURNAL_DEPTH);

    logic [2:0] command[0:1];
    logic [1:0] toggle;
    logic [2:0] engine;
    logic [1:0] op_kind;
    logic op_chip;
    logic [18:0] op_addr;
    logic [7:0] op_data, old_data;
    logic [2:0] group;
    (* ram_style = "distributed" *) logic [30:0] queue[0:JOURNAL_DEPTH-1];
    logic [QUEUE_BITS-1:0] queue_rd, queue_wr;
    logic [QUEUE_BITS:0] queue_count;

    // A flash access is a cartridge ROM access; ROMH selects chip 1.
    wire chip = rom_h;
    wire chip_access = enable && (rom_l || rom_h);
    wire [18:0] flash_addr = {bank, address};
    wire unlock_a = address[10:0] == 11'h555;
    wire unlock_b = address[10:0] == 11'h2aa;
    wire op_active = engine != E_IDLE;
    wire chip_busy = op_active && op_chip == chip;
    wire command_write = chip_access && access_ce && access_we && ultimax && !chip_busy;
    wire queue_push = engine == E_JOURNAL && queue_count != JOURNAL_DEPTH;
    wire queue_pop = journal_valid && journal_ready;

    always_comb begin
        read_override = 1'b0;
        read_data = 8'h00;
        if (chip_access && !access_we) begin
            if (chip_busy) begin
                read_override = 1'b1;
                read_data = {op_kind == OP_PROGRAM ? ~op_data[7] : 1'b0, toggle[chip], 2'b00,
                             op_kind != OP_PROGRAM, 3'b000};
            end else if (command[chip] == CMD_AUTOSELECT) begin
                read_override = 1'b1;
                read_data = address[7:0] == 8'h00 ? 8'h01 : address[7:0] == 8'h01 ? 8'ha4 : 8'h00;
            end
        end
    end

    // Canonical image: bank b's ROML at 0x200000 + b*16 KiB, its ROMH 8 KiB on.
    assign mem_valid = engine == E_READ || engine == E_WRITE;
    assign mem_write = engine == E_WRITE;
    assign mem_addr = {4'b0010, op_addr[18:13], op_chip, op_addr[12:0]};
    assign mem_wdata = old_data & op_data;
    assign erase_valid = engine == E_ERASE;
    assign erase_group = group;
    assign erase_high = op_chip;
    assign journal_valid = queue_count != 0;
    assign journal_data = queue[queue_rd];
    assign busy = op_active || journal_valid;

    always_ff @(posedge clk) begin
        if (queue_push)
            queue[queue_wr] <= {op_kind, op_chip, op_addr, 1'b0, op_data};
    end

    always_ff @(posedge clk) begin
        if (reset || !enable) begin
            command[0] <= CMD_READ;
            command[1] <= CMD_READ;
            toggle <= 2'b00;
            engine <= E_IDLE;
            op_kind <= OP_PROGRAM;
            op_chip <= 1'b0;
            op_addr <= 19'd0;
            op_data <= 8'h00;
            old_data <= 8'h00;
            group <= 3'd0;
            queue_rd <= '0;
            queue_wr <= '0;
            queue_count <= '0;
        end else begin
            if (chip_access && access_ce && !access_we && chip_busy)
                toggle[chip] <= ~toggle[chip];
            if (queue_push) queue_wr <= queue_wr + 1'd1;
            if (queue_pop) queue_rd <= queue_rd + 1'd1;
            queue_count <= queue_count + (queue_push ? 1'd1 : 1'd0) - (queue_pop ? 1'd1 : 1'd0);

            unique case (engine)
                E_JOURNAL:
                    if (queue_push) engine <= op_kind == OP_PROGRAM ? E_READ : E_ERASE;
                E_READ:
                    if (mem_accept) engine <= E_READ_WAIT;
                E_READ_WAIT:
                    if (mem_done) begin
                        old_data <= mem_rdata;
                        engine <= E_WRITE;
                    end
                E_WRITE:
                    if (mem_accept) engine <= E_WRITE_WAIT;
                E_WRITE_WAIT:
                    if (mem_done) engine <= E_IDLE;
                E_ERASE:
                    if (erase_accept) engine <= E_ERASE_WAIT;
                E_ERASE_WAIT:
                    if (erase_done) begin
                        if (op_kind == OP_CHIP_ERASE && group != 3'd7) begin
                            group <= group + 3'd1;
                            engine <= E_ERASE;
                        end else engine <= E_IDLE;
                    end
                default: ;
            endcase

            // A command for one chip while the other is busy is dropped;
            // EasyFlash software waits for each operation to finish.
            if (command_write) begin
                unique case (command[chip])
                    CMD_READ:
                        command[chip] <= write_data == 8'haa && unlock_a ? CMD_UNLOCK1 : CMD_READ;
                    CMD_UNLOCK1:
                        command[chip] <= write_data == 8'h55 && unlock_b ? CMD_UNLOCK2 : CMD_READ;
                    CMD_UNLOCK2: begin
                        command[chip] <= CMD_READ;
                        if (unlock_a) begin
                            if (write_data == 8'h90) command[chip] <= CMD_AUTOSELECT;
                            else if (write_data == 8'ha0) command[chip] <= CMD_PROGRAM;
                            else if (write_data == 8'h80) command[chip] <= CMD_ERASE1;
                        end
                    end
                    CMD_ERASE1:
                        command[chip] <= write_data == 8'haa && unlock_a ? CMD_ERASE2 : CMD_READ;
                    CMD_ERASE2:
                        command[chip] <= write_data == 8'h55 && unlock_b ? CMD_ERASE3 : CMD_READ;
                    CMD_ERASE3: begin
                        command[chip] <= CMD_READ;
                        if (!op_active && (write_data == 8'h30 ||
                                           (write_data == 8'h10 && unlock_a))) begin
                            engine <= E_JOURNAL;
                            op_kind <= write_data == 8'h30 ? OP_SECTOR_ERASE : OP_CHIP_ERASE;
                            op_chip <= chip;
                            op_addr <= flash_addr;
                            op_data <= 8'hff;
                            group <= write_data == 8'h30 ? flash_addr[18:16] : 3'd0;
                        end
                    end
                    CMD_PROGRAM: begin
                        command[chip] <= CMD_READ;
                        if (!op_active) begin
                            engine <= E_JOURNAL;
                            op_kind <= OP_PROGRAM;
                            op_chip <= chip;
                            op_addr <= flash_addr;
                            op_data <= write_data;
                        end
                    end
                    CMD_AUTOSELECT:
                        if (write_data == 8'hf0) command[chip] <= CMD_READ;
                        else if (write_data == 8'haa && unlock_a) command[chip] <= CMD_UNLOCK1;
                    default: command[chip] <= CMD_READ;
                endcase
            end
        end
    end
endmodule
