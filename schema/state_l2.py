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
