# state.l2.K (DESIGN.md 7.8): the MAC table, streamed: K is the entry's
# hardware table index, and get walks the table an entry a step
# (TBL_LUTREAD_NEXT_L2UC) until the search wraps.
#
# Deferred (P5-5): age (not read today).

state("l2", stream=4096, get="om_get_l2",
      tip="MAC addresses by table index; an entry relearned may move")
st("mac", MAC())
st("vlan", U16())
st("port", Port(), tip="The port it was learned on, or cpu")
st("lag", U8(), tip="The port's LAG, 0 none")
st("static", Bool)

# do l2.flush (`l2 forget`; DESIGN.md 8's port argument left out: nothing
# asks for it), do l2.delete K (POST /l2_del.json): an action's argument is
# a number, in om_key. delete_result (DESIGN.md 8) is the action's err line.
obj("l2", order=0, tip="The MAC table")
action("flush", "om_do_l2_flush", tip="Forget the learned entries")
action("delete", "om_do_l2_delete", tip="Delete entry K (state.l2.K); not this switch's own")
message("l2_own", "this switch's own entry")
message("l2_none", "no such entry")
