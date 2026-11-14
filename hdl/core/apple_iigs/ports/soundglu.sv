// Multitini port, 2026-10-14: reproduced by scripts/port_apple_iigs_core.py.
module soundglu
  (input	     CLK_14M,
   input	     clk_7M_en,
   input	     ph0_en,
   input	     reset,
   input	     select,
   input	     wr,
   input [1:0]	     host_addr,
   input [7:0]	     host_data_in,
   input [7:0]	     sound_data_in,
   input	     osc_en,
   output wire      ram_access,
   output reg [7:0]  host_data_out,
   output reg [7:0]  host_read_peek,
   output wire       host_busy,
   output wire [15:0] sound_addr,
   output reg [7:0]  sound_data_out,
   output reg	     ram_wr,
   output reg	     doc_wr,
   output reg	     doc_host_en
   );

   reg              ram_mode;
   reg              auto_increment;
   reg [3:0]        volume;
   reg [15:0]       address_pointer;
   reg [15:0]       active_address;
   reg              pending;
   reg              pending_write;
   reg [7:0]        pending_data;
   reg              finishing;
   reg [1:0]        ram_read_delay;
   reg [7:0]        read_data_reg;

   wire host_commit = select && ph0_en;
   wire host_data_commit = host_commit && host_addr == 1;
   wire control_write = host_commit && wr && host_addr == 0;
   wire service = pending && osc_en;

   // Advance the CPU pointer at the slot and keep the old address stable
   // until the DOC or synchronous sound RAM finishes the transfer.
   assign sound_addr = finishing || (|ram_read_delay) ? active_address : address_pointer;
   assign host_busy = pending || finishing || (|ram_read_delay);
   // Keep the sound module's read mux on RAM until the issued byte arrives.
   assign ram_access = ram_mode || (|ram_read_delay);

   // The accelerated CPU samples this mux without committing another access.
   always @* begin
      case (host_addr)
         0: host_read_peek = {pending, ram_mode, auto_increment, 1'b1, volume};
         1: host_read_peek = read_data_reg;
         2: host_read_peek = address_pointer[7:0];
         3: host_read_peek = address_pointer[15:8];
      endcase
   end

   always @(posedge CLK_14M) begin
      if (reset) begin
         ram_mode <= 0;
         auto_increment <= 0;
         volume <= 0;
         address_pointer <= 0;
         active_address <= 0;
         pending <= 0;
         pending_write <= 0;
         pending_data <= 0;
         finishing <= 0;
         ram_read_delay <= 0;
         read_data_reg <= 0;
         host_data_out <= 0;
         sound_data_out <= 0;
         ram_wr <= 0;
         doc_wr <= 0;
         doc_host_en <= 0;
      end else begin
         finishing <= service;
         ram_read_delay <= {ram_read_delay[0], 1'b0};
         ram_wr <= 0;
         doc_wr <= 0;
         doc_host_en <= 0;

         if (ram_read_delay[1])
            read_data_reg <= sound_data_in;

         if (service) begin
            pending <= 0;
            active_address <= address_pointer;
            sound_data_out <= pending_data;
            if (auto_increment)
               address_pointer <= address_pointer + 16'd1;

            // The control latch can switch the target before the next slot.
            // A mode change on the write slot itself suppresses the write.
            if (pending_write) begin
               if (!(control_write && host_data_in[6] != ram_mode)) begin
                  ram_wr <= ram_mode;
                  doc_wr <= !ram_mode;
                  doc_host_en <= !ram_mode;
               end
            end else if (ram_mode) begin
               // The RAM serves the oscillator on osc_en, then reads the
               // active CPU address on the next edge. Capture one edge later.
               ram_read_delay <= 2'b01;
            end else begin
               read_data_reg <= sound_data_in;
               doc_host_en <= 1;
            end
         end

         if (select && !(wr && ph0_en))
            host_data_out <= host_read_peek;

         if (host_commit && wr) begin
            case (host_addr)
               0: begin
                  ram_mode <= host_data_in[6];
                  auto_increment <= host_data_in[5];
                  volume <= host_data_in[3:0];
               end
               2: address_pointer[7:0] <= host_data_in;
               3: address_pointer[15:8] <= host_data_in;
               default: ;
            endcase
         end

         // A pending access occupies one slot. A new write may replace it,
         // but a busy read only returns the previous value with no side effect.
         if (host_data_commit && (wr || !pending || service)) begin
            pending <= 1;
            pending_write <= wr;
            if (wr)
               pending_data <= host_data_in;
         end
      end
   end
endmodule // soundglu
