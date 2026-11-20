# state.port.N.mib.K (DESIGN.md 7.6): the port's MIB counters, K the STAT
# index 0-54, one STAT_GET an instance (a step each: the 55 took ~5.5 ms).
#
# Deviation (P5-5): the value is the index's raw 64-bit word, and the names
# are only the UI's (schema.js's n, below) and a host's: index 0-7, 46, 47,
# 49 and 50 are one 64-bit counter, the others two 32-bit ones (the high
# word, then the low). The firmware has none of the 87 names (~1.7 KB).

MIB = (["if_in_octets", "if_out_octets", "if_in_ucast", "if_in_mcast", "if_in_bcast", "if_out_ucast",
        "if_out_mcast", "if_out_bcast"] +
       [x.split("/") for x in (
           "if_out_discards/dot1d_tp_port_in_discards dot3_single_collision/dot3_multi_collision "
           "dot3_deferred_tx/dot3_late_collisions dot3_excessive_collisions/dot3_symbol_errors "
           "dot3_ctrl_in_unknown_opcodes/dot3_in_pause dot3_out_pause/ether_drop_events tx_bcast/tx_mcast "
           "tx_crc_align_err/rx_crc_align_err tx_undersize/rx_undersize tx_oversize/rx_oversize "
           "tx_fragments/rx_fragments tx_jabbers/rx_jabbers tx_collisions/tx_64 rx_64/tx_65_127 "
           "rx_65_127/tx_128_255 rx_128_255/tx_256_511 rx_256_511/tx_512_1023 rx_512_1023/tx_1024_1518 "
           "rx_1024_1518/x26_lo rx_undersize_drop/tx_over_1518 rx_over_1518/tx_too_large "
           "rx_too_large/tx_flex_octets_s1 rx_flex_octets_s1/tx_flex_octets_crc_s1 "
           "rx_flex_octets_crc_s1/tx_flex_octets_s0 rx_flex_octets_s0/tx_flex_octets_crc_s0 "
           "rx_flex_octets_crc_s0/length_field_errors false_carriers/undersized_octets framing_errors/x35_lo "
           "rx_mac_discards/rx_mac_ipg_short_drop x37_hi/dot1d_tp_learned_entry_discards "
           "egress_q7_drop/egress_q6_drop egress_q5_drop/egress_q4_drop egress_q3_drop/egress_q2_drop "
           "egress_q1_drop/egress_q0_drop egress_q7_out/egress_q6_out egress_q5_out/egress_q4_out "
           "egress_q3_out/egress_q2_out egress_q1_out/egress_q0_out").split()] +
       ["tx_good", "rx_good", ["rx_err", "tx_err"], "tx_good_phy", "rx_good_phy", ["rx_err_phy", "tx_err_phy"],
        "x52", "x53", "x54"])
assert len(MIB) == 55

state("mib", parent="port", count=55, from0=True, get="om_get_port_mib", names=MIB,
      tip="STAT counters by index: one 64-bit or two 32-bit (high, low)")
st("", U64)
