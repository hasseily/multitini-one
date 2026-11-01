// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// One 512-byte IDE management transfer per coherent command/completion.
// The control-clock RAM port belongs to ARM only while !busy. A descriptor
// hands the whole RAM to the service-clock mover; its completion hands it
// back after the final native falling strobe and RAM writes have retired.
// RAM contents are never synchronized bit by bit. The ownership handshake
// orders independent-clock RAM access. Guest reset/stall errors poison the
// endpoint until PL reset: software cannot safely retry a partial transfer.
module multitini_amiga_ide_block #(
    parameter integer STALL_TIMEOUT_CYCLES=65536
)(
    input wire control_clk,control_resetn,
    input wire host_write,host_read,
    input wire [15:0] host_address,host_read_address,
    input wire [6:0] host_ram_read_address,
    input wire [31:0] host_wdata,input wire [3:0] host_wstrb,
    output reg [31:0] host_rdata,
    input wire legacy_idle,
    output wire busy,legacy_blocked,
    input wire clk,reset,guest_reset,chip_tick,
    output reg native_owned,
    output reg [4:0] ide_address,
    output reg ide_read,ide_write,
    output reg [15:0] ide_wdata,input wire [15:0] ide_rdata
);
    localparam [4:0] BAD_COMMAND=1,OWNERSHIP=2,STALL=3,RESET_ABORT=4,
        LEGACY_BUSY=5,INCOMPLETE_STAGE=6;
    reg in_flight,failed,done,poisoned;
    reg control_alive;
    reg [5:0] control_recovery;
    reg native_epoch_poison;
    wire native_control_alive,owned_control,epoch_poison_control;
    cdc_bit_sync control_alive_i(.clk(clk),.resetn(!reset),.din(control_alive),.dout(native_control_alive));
    cdc_bit_sync owned_control_i(.clk(control_clk),.resetn(control_resetn),.din(native_owned),.dout(owned_control));
    cdc_bit_sync epoch_poison_i(.clk(control_clk),.resetn(control_resetn),.din(native_epoch_poison),.dout(epoch_poison_control));
    assign busy=in_flight || control_recovery!=32 || (owned_control && !epoch_poison_control);
    assign legacy_blocked=in_flight || owned_control;
    reg [4:0] error_code;
    reg [15:0] completed_sequence,active_sequence;
    reg [8:0] completed_words;
    reg [127:0] filled;
    reg command_pending;
    reg [16:0] descriptor;
    wire command_ready,command_valid;
    wire [16:0] native_descriptor;
    wire response_valid,response_ready;
    wire [29:0] response;
    reg response_pending;
    reg [29:0] native_response;
    wire aperture_write=host_address>=16'hd200 && host_address<16'hd400;
    wire aperture_read=host_read_address>=16'hd200 && host_read_address<16'hd400;
    wire full_write=host_wstrb==4'hf && host_address[1:0]==0;
    // Even unused reads must be disabled on the nonowning clock port:
    // independent-clock BRAM read/write collisions are not a safe way to
    // discard a value after it has already passed through the RAM primitive.
    wire ram_host_enable=control_resetn && !busy && !poisoned && !epoch_poison_control;
    wire ram_native_enable=!reset && native_owned;
    wire ram_host_write=host_write && aperture_write && full_write && !busy && !poisoned && !epoch_poison_control;
    wire [6:0] ram_host_address=ram_host_write ? host_address[8:2] : host_ram_read_address;
    wire [31:0] ram_host_q;
    reg [7:0] ram_native_address;
    reg [7:0] ram_native_write_address;
    reg ram_native_write;
    reg [15:0] ram_native_wdata;
    wire [15:0] ram_native_q;
    wire [7:0] ram_native_port_address=ram_native_write ? ram_native_write_address : ram_native_address;
    reg [3:0] state;
    localparam [3:0] IDLE=0,ARM=1,PULSE=2,GAP=3,COMPLETE=4;
    reg direction,pointer_cycle;
    reg [15:0] seq_id;
    reg [8:0] words;
    reg [1:0] gap_count;
    integer stalled;
    wire native_accept=state==IDLE && !response_pending && !native_epoch_poison;

    initial if(STALL_TIMEOUT_CYCLES<8 || STALL_TIMEOUT_CYCLES>16777216)
        $error("IDE block STALL_TIMEOUT_CYCLES must be in8..16777216");

    multitini_async_mailbox #(.WIDTH(17),.DRAIN_ON_RESET(1)) command_i(
        .src_clk(control_clk),.src_resetn(control_resetn),.src_data(descriptor),
        .src_valid(command_pending),.src_ready(command_ready),
        .dst_clk(clk),.dst_resetn(!reset),.dst_data(native_descriptor),
        .dst_valid(command_valid),.dst_ready(native_accept));
    multitini_async_mailbox #(.WIDTH(30),.DRAIN_ON_RESET(1)) completion_i(
        .src_clk(clk),.src_resetn(!reset),.src_data(native_response),
        .src_valid(response_pending),.src_ready(response_ready),
        .dst_clk(control_clk),.dst_resetn(control_resetn),.dst_data(response),
        .dst_valid(response_valid),.dst_ready(1'b1));

    always @* begin
        host_rdata=0;
        case(host_read_address)
            16'hd100:host_rdata={busy,failed,done,error_code,8'd0,completed_sequence};
            16'hd108:host_rdata={23'd0,completed_words};
            default:if(aperture_read && !busy && !poisoned && filled[host_ram_read_address])
                host_rdata=ram_host_q;
        endcase
    end
    always @(posedge control_clk) begin
        if(!control_resetn)begin
            in_flight<=0;failed<=0;done<=0;poisoned<=0;error_code<=0;
            control_alive<=0;control_recovery<=0;
            completed_sequence<=0;active_sequence<=0;completed_words<=0;
            filled<=0;command_pending<=0;descriptor<=0;
        end else begin
            control_alive<=1;
            if(control_recovery!=32)control_recovery<=control_recovery+1'b1;
            // An independently reset control domain cannot recycle a RAM
            // still owned by the mover. The service domain aborts that old
            // epoch and retains its poison until a complete PL reset.
            if(epoch_poison_control)begin
                poisoned<=1;failed<=1;done<=1;error_code<=RESET_ABORT;in_flight<=0;command_pending<=0;
            end
            if(command_pending && command_ready)command_pending<=0;
            if(response_valid && in_flight && response[29:14]==active_sequence)begin
                in_flight<=0;done<=1;completed_sequence<=response[29:14];
                completed_words<=response[8:0];
                if(response[13:9]!=0)begin
                    failed<=1;error_code<=response[13:9];
                    if(response[13:9]==STALL || response[13:9]==RESET_ABORT)poisoned<=1;
                end
                if(response[13:9]==0 && descriptor[0])filled<=128'hffffffffffffffffffffffffffffffff;
            end
            if(host_read && aperture_read && busy)begin failed<=1;error_code<=OWNERSHIP;end
            if(host_write)begin
                if(aperture_write)begin
                    if(busy || !full_write)begin failed<=1;error_code<=OWNERSHIP;end
                    else if(!poisoned && !epoch_poison_control)filled[host_address[8:2]]<=1;
                end else if(host_address==16'hd104)begin
                    if(busy)begin failed<=1;error_code<=OWNERSHIP;end
                    else if(!poisoned && !epoch_poison_control)begin
                        if(!full_write || host_wdata[31:16]==0 ||
                                host_wdata[31:16]==completed_sequence || host_wdata[15:1]!=15'd256)begin
                            failed<=1;done<=1;error_code<=BAD_COMMAND;
                            completed_sequence<=host_wdata[31:16];completed_words<=0;
                        end else if(!legacy_idle || !command_ready)begin
                            failed<=1;done<=1;error_code<=LEGACY_BUSY;
                            completed_sequence<=host_wdata[31:16];completed_words<=0;
                        end else if(!host_wdata[0] && !(&filled))begin
                            failed<=1;done<=1;error_code<=INCOMPLETE_STAGE;
                            completed_sequence<=host_wdata[31:16];completed_words<=0;
                        end else begin
                            descriptor<={host_wdata[31:16],host_wdata[0]};command_pending<=1;
                            active_sequence<=host_wdata[31:16];in_flight<=1;done<=0;failed<=0;
                            error_code<=0;completed_words<=0;filled<=0;
                        end
                    end
                end
            end
        end
    end

    // These are staged service-clock signals. The enclosing host publishes
    // the whole muxed bundle at negedge clk before its accepted native edge.
    always @(posedge clk) begin
        ram_native_write<=0;
        if(reset)begin
            state<=IDLE;native_owned<=0;ide_address<=0;ide_read<=0;ide_write<=0;ide_wdata<=0;
            native_epoch_poison<=0;
            seq_id<=0;direction<=0;pointer_cycle<=0;words<=0;gap_count<=0;stalled<=0;
            response_pending<=0;native_response<=0;ram_native_address<=0;ram_native_write_address<=0;ram_native_wdata<=0;
        end else begin
            if(response_pending && response_ready)begin
                response_pending<=0;native_owned<=native_epoch_poison;state<=IDLE;
            end
            if(state!=IDLE && state!=COMPLETE)begin
                if(chip_tick)stalled<=0;
                else if(stalled<STALL_TIMEOUT_CYCLES-1)stalled<=stalled+1;
            end
            if(native_owned && !native_control_alive && !native_epoch_poison)begin
                native_epoch_poison<=1;ide_read<=0;ide_write<=0;
                native_response<={seq_id,RESET_ABORT,words};response_pending<=1;state<=COMPLETE;
            end else if(state!=IDLE && state!=COMPLETE &&
                    ((guest_reset===1'b1) || (!chip_tick && stalled==STALL_TIMEOUT_CYCLES-1)))begin
                ide_read<=0;ide_write<=0;
                native_response<={seq_id,(guest_reset===1'b1) ? RESET_ABORT:STALL,words};
                response_pending<=1;state<=COMPLETE;
            end else case(state)
                IDLE:if(command_valid && !response_pending)begin
                    seq_id<=native_descriptor[16:1];direction<=native_descriptor[0];
                    native_owned<=1;words<=0;ram_native_address<=0;stalled<=0;
                    ide_address<=14;ide_wdata<=0;ide_read<=0;ide_write<=0;pointer_cycle<=1;
                    if(guest_reset===1'b1)begin
                        native_response<={native_descriptor[16:1],RESET_ABORT,9'd0};
                        response_pending<=1;state<=COMPLETE;
                    end else state<=ARM;
                end
                ARM:if(chip_tick)begin ide_write<=1;state<=PULSE;end
                PULSE:if(chip_tick)begin
                    ide_read<=0;ide_write<=0;gap_count<=0;state<=GAP;
                    if(!pointer_cycle)begin
                        words<=words+1'b1;
                        if(direction)begin
                            ram_native_wdata<=ide_rdata;ram_native_write_address<=ram_native_address;ram_native_write<=1;
                        end
                        if(ram_native_address!=255)ram_native_address<=ram_native_address+1'b1;
                    end
                end
                GAP:if(chip_tick)begin
                    // Three accepted low edges retire the falling strobe,
                    // native FIFO RAM read and registered mgmt_readdata.
                    if(gap_count!=2)gap_count<=gap_count+1'b1;
                    else if(!pointer_cycle && words==256)begin
                        native_response<={seq_id,5'd0,words};response_pending<=1;state<=COMPLETE;
                    end else begin
                        pointer_cycle<=0;ide_address<=15;ide_wdata<=ram_native_q;
                        ide_read<=direction;ide_write<=!direction;state<=PULSE;
                    end
                end
                COMPLETE:;
                default:state<=IDLE;
            endcase
        end
    end

`ifdef SYNTHESIS
    xpm_memory_tdpram #(
        .ADDR_WIDTH_A(7),.ADDR_WIDTH_B(8),.AUTO_SLEEP_TIME(0),
        .BYTE_WRITE_WIDTH_A(32),.BYTE_WRITE_WIDTH_B(16),
        .CLOCKING_MODE("independent_clock"),.ECC_MODE("no_ecc"),
        .MEMORY_INIT_FILE("none"),.MEMORY_INIT_PARAM("0"),
        .MEMORY_OPTIMIZATION("true"),.MEMORY_PRIMITIVE("block"),
        .MEMORY_SIZE(4096),.MESSAGE_CONTROL(0),
        .READ_DATA_WIDTH_A(32),.READ_DATA_WIDTH_B(16),.READ_LATENCY_A(1),.READ_LATENCY_B(1),
        .READ_RESET_VALUE_A("0"),.READ_RESET_VALUE_B("0"),.RST_MODE_A("SYNC"),.RST_MODE_B("SYNC"),
        .SIM_ASSERT_CHK(1),.USE_EMBEDDED_CONSTRAINT(0),.USE_MEM_INIT(0),.WAKEUP_TIME("disable_sleep"),
        .WRITE_DATA_WIDTH_A(32),.WRITE_DATA_WIDTH_B(16),.WRITE_MODE_A("read_first"),.WRITE_MODE_B("read_first")
    ) ram_i(
        .clka(control_clk),.clkb(clk),.ena(ram_host_enable),.enb(ram_native_enable),
        .addra(ram_host_address),.addrb(ram_native_port_address),.dina(host_wdata),.dinb(ram_native_wdata),
        .wea(ram_host_write),.web(ram_native_write),.douta(ram_host_q),.doutb(ram_native_q),
        .regcea(1'b1),.regceb(1'b1),.rsta(1'b0),.rstb(1'b0),.sleep(1'b0),
        .injectdbiterra(1'b0),.injectdbiterrb(1'b0),.injectsbiterra(1'b0),.injectsbiterrb(1'b0),
        .dbiterra(),.dbiterrb(),.sbiterra(),.sbiterrb());
`else
    reg [15:0] memory[0:255];
    reg [31:0] host_q;
    reg [15:0] native_q;
    assign ram_host_q=host_q,ram_native_q=native_q;
    always @(posedge control_clk)if(ram_host_enable)begin
        if(ram_host_write)begin memory[{ram_host_address,1'b0}]<=host_wdata[15:0];memory[{ram_host_address,1'b1}]<=host_wdata[31:16];end
        host_q<={memory[{ram_host_address,1'b1}],memory[{ram_host_address,1'b0}]};
    end
    always @(posedge clk)if(ram_native_enable)begin
        if(ram_native_write)memory[ram_native_port_address]<=ram_native_wdata;
        native_q<=memory[ram_native_port_address];
    end
`endif
endmodule
