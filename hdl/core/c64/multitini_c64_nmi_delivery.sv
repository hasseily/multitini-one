// SPDX-License-Identifier: GPL-3.0-only
// DESIGN 2.9: live_nmi_n has already been registered in the fast domain.
module multitini_c64_nmi_delivery (
    input wire clk, reset, turbo, t65_enable,
    input wire live_nmi_n, exact_nmi_n,
    output wire t65_nmi_n,
    output wire exact_switch_safe,
    output wire pending_rearm,
    output wire carry_active,
    output wire used_hold
);
    reg native_previous = 1;
    // Match T65's unreset NMI_n_o: learn only at an actual non-reset enable.
    reg seen;
    reg fell = 0, force_low = 0, carry = 0, previous_turbo = 0;
    wire switching_exact = previous_turbo && !turbo;
    assign carry_active = carry || switching_exact;
    wire delivery_active = turbo || carry_active;
    wire falling_now = native_previous && !live_nmi_n;
    wire any_fall = fell || falling_now;
    wire shown = force_low ? 1'b0 :
                 any_fall ? (seen ? 1'b0 : 1'b1) :
                 turbo ? live_nmi_n : exact_nmi_n;
    wire force_next = !force_low && any_fall && !seen;
    assign t65_nmi_n = delivery_active ? shown : exact_nmi_n;
    assign exact_switch_safe = !force_next && (shown == live_nmi_n);
    assign pending_rearm = force_low;
    assign used_hold = delivery_active && t65_enable && (any_fall || force_low);
    always @(posedge clk) begin
        native_previous <= live_nmi_n;
        previous_turbo <= turbo;
        if (reset) begin
            fell <= 0;
            force_low <= 0;
            carry <= 0;
            previous_turbo <= 0;
        end else begin
            // Adoption of turbo ends only the carry bookkeeping. A rearm
            // already in flight remains live and is completed in turbo.
            if (turbo) carry <= 0;
            if (delivery_active && t65_enable) begin
                fell <= 0; // shown already includes a same-edge fall.
                force_low <= force_next;
                if (!turbo) carry <= force_next || (shown != exact_nmi_n);
            end else if (delivery_active) begin
                if (falling_now) fell <= 1;
                if (switching_exact) carry <= 1;
            end else begin
                // Steady exact mode samples B0 only, exactly as stock.
                fell <= 0;
                force_low <= 0;
                carry <= 0;
            end
        end
        if (!reset && t65_enable) seen <= t65_nmi_n;
    end
endmodule
