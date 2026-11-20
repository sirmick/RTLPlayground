# state.cfg: the staging (DESIGN.md 5, 7.12). The engine's own state:
# om.c fills it.

state("cfg", get="om_get_cfg", tip="The candidate, running and the last apply")
st("pending", U16(), tip="Instances where the candidate differs from running")
st("dirty", Bool, tip="Running differs from the saved config")
st("apply", Enum("cfgapply", "idle running confirm undo done failed rolled_back"),
   tip="The last apply: confirm is a window open, undo a rollback running")
st("confirm_s", U16(), tip="Seconds left in the confirm window")
st("format", Enum("cfgformat", "ok blank bad legacy"),
   tip="What boot found in the saved config: legacy is old command text (booted on the default)")
st("save", Enum("cfgsave", "idle erasing writing done failed"), tip="The last save")
st("saved_bytes", U16(), tip="The saved config's size in flash")
st("load.skipped", U16(), tip="Records the last load skipped: unknown, or out of range")
