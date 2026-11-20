# state.cfg: the staging (DESIGN.md 5, 7.12). The engine's own state:
# om.c fills it.

state("cfg", get="om_get_cfg", tip="The candidate, running and the last apply")
st("pending", U16(), tip="Instances where the candidate differs from running")
st("dirty", Bool, tip="Running differs from the saved config")
st("apply", Enum("cfgapply", "idle running confirm undo done failed rolled_back"),
   tip="The last apply: confirm is a window open, undo a rollback running")
st("confirm_s", U16(), tip="Seconds left in the confirm window")
