"use strict";
var LANG = {
    en: {
        nav_dash: "Dashboard",
        nav_ports: "Ports",
        nav_stp: "Spanning tree",
        nav_stats: "Statistics",
        nav_vlan: "VLANs",
        nav_l2: "MAC table",
        nav_mirror: "Mirroring",
        nav_lag: "LAG",
        nav_bw: "Bandwidth",
        nav_system: "System",
        nav_fw: "Firmware",
        nav_schema: "Schema",
        nav_lldp: "LLDP",
        hdr_dirty: "unsaved changes",
        hdr_dirty_t: "Running config differs from startup config",
        hdr_save: "Save to flash",
        hdr_save_t: "Persist running configuration to flash",
        th_auto: "System",
        th_auto_sel: "System (Selenized)",
        sy_display: "Display",
        sy_leds: "LEDs",
        sy_leds_l: "Front panel",
        sy_leds_note: "Applies at once; Save to flash keeps it across restarts.",
        led_on: "On",
        led_ports: "Port LEDs off",
        led_off: "All off",
        sy_theme: "Theme",
        sy_display_note: "Stored in this browser only.",
        th_light: "Light",
        th_dark: "Dark",
        th_sel_light: "Selenized Light",
        th_sel_dark: "Selenized Dark",
        c_port: "Port",
        pt_state: "Port state",
        om_str: "printable text, at most {n} characters, no \" or \\",
        stg_n: "{n} staged",
        stg_diff: "Diff",
        stg_discard: "Discard",
        stg_apply: "Apply",
        stg_none: "Nothing staged",
        stg_save: "Save",
        stg_saved: "Saved",
        stg_applied: "Applied",
        stg_busy: "Wait for the apply window to end",
        stg_rolled: "Not confirmed: the switch rolled the change back",
        stg_lost: "No answer from the switch",
        stg_unapplied: "Not applied: the change is still staged",
        stg_keep_q: "Keep the last change?",
        stg_keep_d: "The switch rolls it back in {s} s unless it is confirmed. It may be another client's change: keep it only if it is yours.",
        sy_move_d: "The management IP changes to {ip}. Open it, log in and keep the change within 2 min, or the switch goes back to {old}.",
        sy_back: "Unconfirmed, the switch is back at {u} after 2 min",
        sy_dhcp_w: "The switch requests an address via DHCP. There is no rollback: you must find its new IP to reconnect.",
        sy_pw_w: "Log in again with the new password and keep the change within 2 min, or the switch goes back to the old one.",
        stg_discarded: "{e}: the staged changes are discarded",
        stg_failed: "Apply failed: the switch runs the old config",
        stg_confirm: "confirming, {s} s",
        c_name: "Name",
        c_link: "Link",
        c_apply: "Apply",
        c_cancel: "Cancel",
        c_confirm: "Confirm",
        c_close: "Close",
        c_refresh: "Refresh",
        c_off: "off",
        c_offc: "Off",
        c_down: "down",
        c_on: "On",
        c_auto: "Auto",
        c_type: "Type",
        c_state: "State",
        c_enabled: "Enabled",
        c_disable: "Disable",
        c_full: "full",
        c_half: "half",
        c_yes: "yes",
        c_no: "no",
        d_system: "System",
        d_traffic: "Traffic",
        d_traffic_h: "packets/s, live",
        d_txpps: "TX pps",
        d_rxpps: "RX pps",
        d_txbad: "TX bad",
        d_rxbad: "RX bad",
        i_mac: "MAC",
        i_fw: "Firmware",
        i_built: "Built",
        i_uptime: "Uptime",
        i_uptime_d: "{d} d {t}",
        i_hw: "Hardware",
        i_pcb: "PCB",
        i_temp: "Temperature",
        i_flash: "Flash",
        i_reset: "Last reset",
        p_state: "State",
        p_disabled: "disabled",
        p_up: "up",
        p_pkts: "pkts",
        p_module: "Module",
        p_temp: "Temperature",
        p_vcc: "Vcc",
        p_txbias: "TX bias",
        p_txpower: "TX power",
        p_rxpower: "RX power",
        p_txfault: "TX fault",
        p_txdis: "TX disabled",
        p_rxlos: "RX LOS",
        pt_title: "Port configuration",
        f_port_duplex: "Duplex",
        f_port_ingress_filter: "Filter",
        f_port_eee: "EEE",
        f_port_isolate: "Sends to",
        f_port_cabletest: "Cable test",
        f_state_port_duplex: "Duplex",
        f_state_port_media: "Media",
        f_state_port_ms_role: "Role",
        f_state_port_eee_active: "EEE active",
        pt_conn: "Devices learned",
        pt_speed: "Speed",
        pt_mtu: "MTU",
        stp_title: "Spanning tree",
        loop_title: "Loop detection",
        loop_d: "The switch sends a probe on every port and watches for its own probes coming back. While spanning tree is off it blocks one port of each loop it finds, until the loop is gone; with spanning tree on, loops are only reported.",
        loop_none: "No loop",
        loop_off: "Off",
        loop_looped: "Looped ports: {p}",
        loop_blocked: "blocked: {p}",
        loop_stp: "spanning tree is on: reported only",
        stp_h: "RSTP, 802.1w",
        stp_version: "Version",
        stp_v_stp: "STP compatible",
        stp_prio: "Bridge priority",
        stp_hello: "Hello [s]",
        stp_maxage: "Max age [s]",
        stp_fwd: "Forward delay [s]",
        stp_txhold: "TX hold",
        stp_bridge_apply: "Apply bridge settings",
        stp_off_msg: "Spanning tree is disabled: all ports forward.",
        stp_bridge: "Bridge",
        stp_root: "Root",
        stp_root_self: "this bridge is the root",
        stp_via: "via port",
        stp_cost: "path cost",
        stp_tc: "topology changes",
        stp_prio_note: "Lower priority wins the root election (default 8 = 32768).",
        stp_portcfg: "Port configuration",
        stp_edge: "Edge",
        stp_pcost: "Path cost",
        stp_pprio: "Priority",
        stp_guard: "Guard",
        stp_filter: "BPDU filter",
        stp_p2p: "Point-to-point",
        stp_port_note: "Edge ports forward immediately and cause no topology change; Auto treats a port as edge after 3 s without BPDUs, and any received BPDU revokes it. Path cost 0 = automatic.",
        stp_status: "Port status",
        stp_role: "Role",
        stp_db: "Designated bridge",
        stp_dp: "Designated port",
        stp_dc: "Designated cost",
        stp_oedge: "Oper. edge",
        stp_op2p: "Oper. P2P",
        stp_s0: "disabled",
        stp_s1: "blocking",
        stp_s2: "learning",
        stp_s3: "forwarding",
        stp_r1: "Root",
        stp_r2: "Designated",
        stp_r3: "Alternate",
        stp_trip: "guard tripped",
        stp_g_none: "None",
        stp_g_bpdu: "BPDU",
        stp_g_root: "Root",
        stp_en_q: "Enable spanning tree?",
        stp_en_d: "Ports start blocked and take up to twice the forward delay to reach forwarding; edge ports recover immediately.",
        stp_dis_q: "Disable spanning tree?",
        stp_dis_d: "All ports go straight to forwarding; loop protection is lost.",
        stp_cost_err: "Path cost must be 0-200000000",
        lldp_enabled: "LLDP Enabled",
        lldp_title: "Service Status",
        lldp_portname: "Name",
        lldp_enable: "Send Frames",
        stp_pcs: "Path cost",
        stp_pcs_long: "Long (32 bit)",
        stp_pcs_short: "Short (16 bit)",
        stp_bh: "BPDUs while disabled",
        stp_bh_flood: "Flood",
        stp_bh_filter: "Filter",
        stp_r4: "Backup",
        stp_times: "max age / forward delay",
        stp_proto: "Protocol",
        stp_nolink: "no link",
        stp_lasttc: "last",
        stp_mcheck: "Check",
        stp_unguard: "Clear",
        stp_mcheck_t: "Send RST BPDUs again to find out whether the neighbour still needs 802.1D",
        stp_cnt: "Counters",
        stp_crx: "BPDUs received",
        stp_ctx: "BPDUs sent",
        stp_ctrx: "TC received",
        stp_cttx: "TC sent",
        stp_cage: "Last BPDU [s]",
        st_title: "Port statistics",
        st_h: "totals since boot",
        st_txg: "TX good",
        st_txb: "TX bad",
        st_rxg: "RX good",
        st_rxb: "RX bad",
        st_details: "Details",
        st_counters: "MIB counters",
        st_nonzero: "non-zero only",
        st_autoref: "auto-refresh",
        st_counter: "Counter",
        st_value: "Value",
        st_fail: "failed to load counters",
        v_title: "VLANs",
        v_vid: "VLAN ID",
        v_members: "Members",
        v_tagged: "Tagged",
        v_untagged: "Untagged",
        v_pvid_on: "PVID on",
        v_empty: "No VLANs configured.",
        v_mgmt: "Management VLAN",
        v_editor: "VLAN editor",
        v_optional: "optional",
        v_setmgmt_t: "Management VLAN",
        v_member: "Member",
        v_pvid: "PVID",
        v_add: "Add a new VLAN",
        v_legend: "U = untagged member, T = tagged member, - = not a member. An untagged member takes this VLAN as its PVID; a port whose PVID it was and that is no longer untagged here goes to PVID 0. Marked rows are staged, not yet running.",
        v_accept: "Accept",
        v_del_t: "Delete VLAN",
        v_rename: "Rename",
        v_rename_t: "Rename VLAN",
        v_del_q: "Delete VLAN {n}?",
        v_del_d: "Ports keep their PVID until reassigned.",
        v_vid_err: "Enter a VLAN ID (1-4094)",
        v_name_rule: "A name starts with a letter: up to 15 printable characters, no space, \" or \\",
        v_loaded: "Loaded VLAN {n}",
        v_notfound: "VLAN {n} not found (a new one can still be created)",
        v_nomember: "Select at least one member port (or delete the VLAN instead)",
        v_vid_err: "VLAN ID already exists",
        v_mgmt_tt: "The management VLAN cannot be deleted, you need to assign management to another VLAN first.",
        l2_title: "MAC address table",
        l2_filter: "filter...",
        l2_flush: "Flush learned entries",
        l2_static: "static",
        l2_learned: "learned",
        l2_loading: "loading...",
        l2_failed: "load failed",
        l2_entries: "entries",
        l2_del_t: "Delete entry",
        l2_flush_q: "Flush all learned MAC entries?",
        m_title: "Port mirroring",
        m_active: "active",
        m_monitor: "Monitor port",
        m_mirror: "Mirror",
        m_both: "Both",
        m_note: "Both = mirror RX and TX of the port to the monitor port.",
        m_none: "Select at least one mirrored port",
        lag_hash: "Hash:",
        lag_hash_none: "hash needs a field",
        lag_pvid_moved: "LAG ports {p} take one PVID: {v}",
        lag_other: "Port {p} is in another LAG: take it out there first",
        lacp_heading: "LACP (802.3ad)",
        lag_mode: "Mode",
        lag_static: "Static",
        lacp_neg: "negotiating",
        lacp_agg: "aggregator",
        lacp_members: "active members",
        lacp_actor: "Actor",
        lacp_partner: "Partner",
        lacp_rxstate: "RX state",
        lacp_rx: "LACPDUs received",
        lacp_psys: "Partner system",
        lag_clear_q: "Clear LAG {n}?",
        lag_clear_d: "All member ports return to normal operation.",
        bw_title: "Bandwidth limits",
        bw_h: "Mbit/s, 0.016-10000",
        bw_in: "Ingress limit",
        bw_out: "Egress limit",
        bw_exceed: "When exceeded",
        bw_fc: "Flow control",
        bw_drop: "Drop",
        bw_in_err: "Ingress limit must be 0.016-10000 Mbit/s",
        bw_out_err: "Egress limit must be 0.016-10000 Mbit/s",
        sc_title: "Storm control",
        sc_h: "per port and traffic type, empty = off",
        sc_bcast: "Broadcast",
        sc_mcast: "Multicast",
        sc_ucast: "Unknown unicast",
        sc_umcast: "Unknown multicast",
        sc_err: "Storm limit must be 1-1048575 pps or 1-10000000 kbit/s",
        sc_note: "Frames of a type above its limit are dropped where they enter the switch.",
        sy_network: "Network",
        sy_mode: "Addressing",
        sy_static: "Static",
        sy_dhcp_t: "Request address via DHCP",
        sy_services: "Services",
        sy_igmp: "IGMP snooping",
        sy_sysip: "server IP",
        sy_server: "Server",
        sy_port: "Port",
        sy_sesstmo: "Session timeout (s)",
        sy_sesstmo_err: "Session timeout must be 1-65535 s",
        sy_services_note: "Service state reflects the startup config; runtime state is not readable.",
        sy_password: "Admin password",
        sy_newpw: "New password",
        sy_repeat: "Repeat",
        sy_pwapply: "Change password",
        sy_pw_note: "Takes effect immediately; save to flash to keep it after reboot.",
        sy_console: "Console",
        sy_cmd: "CLI command...",
        sy_send: "Send",
        sy_startup: "Startup configuration",
        sy_replayed: "replayed on every boot",
        sy_reload: "Reload from flash",
        sy_write: "Write to flash",
        sy_unknown_note: "Unknown lines are highlighted before writing.",
        sy_maint: "Maintenance",
        sy_reboot: "Reboot switch",
        sy_lang: "Language",
        sy_ip_err: "Invalid IP / netmask / gateway",
        sy_host_err: "Hostname: 1-23 printable characters, no spaces or quotes",
        sy_net_q: "Apply network settings?",
        sy_ip_changed: "IP changed, reconnect at http://{ip}/",
        sy_dhcp_q: "Switch to DHCP?",
        sy_sysip_err: "Invalid syslog server IP",
        sy_dns1: "DNS server",
        sy_dns2: "Second DNS",
        sy_dns_err: "Invalid DNS server IP",
        sy_dns_dhcp: "from DHCP: {ip}",
        sy_time: "Time",
        sy_ntp: "NTP",
        sy_ntpsrv: "NTP server",
        sy_ntpsrv_p: "host name or IPv4 address",
        sy_ntpint: "Poll interval [min]",
        sy_tz: "Time zone",
        sy_dst: "Daylight saving",
        sy_dst_eu: "Europe",
        sy_dst_us: "United States",
        sy_now: "Local time",
        sy_sync: "Last synchronisation",
        sy_ntpaddr: "Server address",
        sy_nosync: "not synchronised",
        sy_waiting: "waiting for the server",
        sy_ago: "{n} min ago",
        sy_ntp_err: "Enter the host name or IPv4 address of the NTP server",
        sy_ntpint_err: "Poll interval must be 1-1440",
        sy_time_note: "The switch has no battery backed clock: the time is lost at every restart and set again from the NTP server once the network is up. Save to flash to keep the settings.",
        sy_sysport_err: "Syslog port must be 1-65535",
        sy_pw_len: "Password: 1-20 characters",
        sy_pw_space: "Password cannot contain spaces",
        sy_pw_match: "Passwords do not match",
        sy_pw_q: "Change admin password?",
        sy_reboot_q: "Reboot the switch?",
        sy_reboot_d: "There are UNSAVED changes and they will be lost. Save to flash first if you want to keep them.",
        sy_rebooting: "Rebooting, reconnect in about 20 s",
        sy_bytes: "bytes",
        cw_title: "Write this startup configuration?",
        cw_save_title: "Save running configuration to flash?",
        cw_info: "{n} / 2048 bytes, replayed line by line on every boot",
        cw_toolarge: "Too large: the config sector accepts at most 2048 bytes. Remove lines first.",
        cw_longline: "Line {n} is {m} bytes long: the switch can only replay lines up to 126 bytes and would skip it on boot, so the write is refused. Shorten it first.",
        cw_unknown: "Not in the known config grammar (will still be written): ",
        cw_empty: "(empty)",
        cw_writing: "Writing configuration...",
        cw_failed: "config write failed: HTTP {n}",
        cw_verify_fail: "Verification failed: flash content differs from what was sent. Command log NOT cleared.",
        cw_saved: "Startup configuration saved and verified",
        cw_collect: "Collecting running changes...",
        t_applied: "Applied: {c}",
        t_cmds: "{n} command(s) applied",
        t_rejected: "command rejected: {c}",
        t_failed: "failed: {c}",
        fw_title: "Firmware update",
        fw_intro: "The application: the -install image (the application with the loader; a switch without the loader gets it on the way), or a plain application image. It is staged to flash and CRC-checked; on success the switch reboots and installs it. The startup configuration is kept.",
        sy_download: "Download",
        sy_loadfile: "Load file...",
        sy_loaded_note: "Loaded into the editor: check it, then Write to flash",
        fw_status: "This switch",
        fw_s_ver: "Firmware",
        fw_s_loader: "Loader",
        fw_s_poe: "PoE controller",
        fw_s_yes: "yes: updates are installed by the loader",
        fw_s_no: "no: the next -install image adds it",
        fw_poe_ok: "firmware loaded",
        fw_poe_none: "no firmware: upload it below, PoE is off",
        fw_poe_noanswer: "no answer from the controller",
        fw_poe_nochip: "the firmware has no image for this controller",
        fw_poe_noroom: "no room for its firmware on this flash",
        fw_poe_failed: "loading its firmware failed",
        fw_inst_valid: "valid install image: the application and the loader (sizes, magic and both CRC16 OK)",
        fw_wrong_card: "this is not for this card: use \u201c{c}\u201d",
        pf_title: "PoE controller firmware",
        pf_intro: "The machine data image with the PoE controller's firmware (MDT1). It is written in its own flash region, which no firmware update touches, and loaded at the next boot.",
        ld_title: "Loader",
        ld_intro: "The 16 KiB loader, which installs application updates and recovers a switch whose application fails. Only needed when the loader itself changes: the -install image already brings it to a switch without one.",
        ld_none: "This switch has no loader yet. Uploading it here (or an -install image above) converts the switch at the next boot.",
        ld_convert_d: "The switch reboots and converts itself to the loader at the next boot, then starts the same application. Do not power it off for a minute after the upload: a power cut while the loader is written (about 8 s) needs a flash programmer.",
        fw_upload: "Upload",
        fw_checking: "{f}: {n} bytes. Checking...",
        fw_size_err: "size is {n}: expected {w} (application), {w} + 16384 (install image) or 16384 (loader)",
        fw_magic_err: "missing bank header / LJMP magic: not a firmware image",
        fw_crc_err: "CRC16 check failed: corrupt image",
        fw_valid: "valid RTLPlayground image (size, magic and CRC16 OK)",
        fw_ldr_valid: "valid loader image (size, magic and CRC16 OK)",
        fw_md_valid: "valid machine data (this switch's PoE firmware, CRC16 OK)",
        fw_md_d: "It is written in place and used from the next boot; nothing else on the switch changes.",
        fw_md_done: "machine data written, used from the next boot ✓",
        fw_ldr_d: "The running firmware rewrites the loader and then resets the switch. Do not power it off until it comes back: a loader cut off mid-write needs a flash programmer.",
        fw_ldr_failed: "no reboot detected: writing the loader failed. Do not power off the switch, upload the loader again or check the serial console.",
        fw_q: "Upload firmware?",
        fw_other: "This image was not built for this switch ({m}): its SFP, LED and button pins may differ. Upload it only if you are sure.",
        fw_d: "On a good checksum the switch resets itself and applies the image during boot (the startup config is preserved). Do not power it off until it comes back.",
        fw_finishing: "finishing flash write... {s} s",
        fw_uploading: "uploading... {p}% / {s} s",
        fw_verified: "checksum verified, the switch is rebooting...",
        fw_rejected: "the switch rejected the image (bad checksum), nothing was applied",
        fw_lost: "upload failed: connection lost mid-transfer",
        fw_noreboot: "no reboot detected: the image was most likely rejected. If updating from old firmware, verify the version in the sidebar after logging in again.",
        fw_applied: "update applied",
        fw_done_t: "Firmware updated",
        fw_done: "The switch verified the image and rebooted into it. The session was reset, so you will be asked to log in again.",
        fw_login: "Go to login",
        fw_rebooting: "switch is rebooting...",
        fw_timeout: "switch has not come back after 150 s: check power / serial console"
    },
    ja: {
        nav_dash: "ダッシュボード",
        nav_ports: "ポート",
        nav_stp: "スパニングツリー",
        nav_stats: "統計",
        nav_vlan: "VLAN",
        nav_l2: "MAC テーブル",
        nav_mirror: "ミラーリング",
        nav_lag: "LAG",
        nav_bw: "帯域制限",
        nav_system: "システム",
        nav_fw: "ファームウェア",
        hdr_dirty: "未保存の変更",
        hdr_dirty_t: "実行中の設定が起動設定と異なります",
        hdr_save: "フラッシュに保存",
        hdr_save_t: "実行中の設定をフラッシュに書き込む",
        th_auto: "システム",
        th_auto_sel: "システム (Selenized)",
        sy_display: "表示",
        sy_theme: "テーマ",
        sy_display_note: "このブラウザーにのみ保存されます。",
        th_light: "ライト",
        th_dark: "ダーク",
        th_sel_light: "Selenized ライト",
        th_sel_dark: "Selenized ダーク",
        c_port: "ポート",
        c_name: "名前",
        c_link: "リンク",
        c_apply: "適用",
        c_cancel: "キャンセル",
        c_confirm: "確認",
        c_close: "閉じる",
        c_refresh: "更新",
        c_off: "オフ",
        c_offc: "オフ",
        c_down: "リンクダウン",
        c_on: "オン",
        c_auto: "自動",
        c_type: "タイプ",
        c_state: "状態",
        c_enabled: "有効",
        c_disable: "無効化",
        c_full: "全二重",
        c_half: "半二重",
        c_yes: "はい",
        c_no: "いいえ",
        d_system: "システム",
        d_traffic: "トラフィック",
        d_traffic_h: "パケット/秒、リアルタイム",
        d_txpps: "TX pps",
        d_rxpps: "RX pps",
        d_txbad: "TX 異常",
        d_rxbad: "RX 異常",
        i_mac: "MAC",
        i_fw: "ファームウェア",
        i_built: "ビルド日",
        i_uptime: "稼働時間",
        i_uptime_d: "{d}日 {t}",
        i_hw: "ハードウェア",
        i_pcb: "PCB",
        i_temp: "温度",
        i_flash: "フラッシュ",
        p_state: "状態",
        p_disabled: "無効",
        p_up: "アップ",
        p_pkts: "pkts",
        p_module: "モジュール",
        p_temp: "温度",
        p_vcc: "電圧",
        p_txbias: "TX バイアス",
        p_txpower: "TX 電力",
        p_rxpower: "RX 電力",
        p_txfault: "TX 障害",
        p_txdis: "TX 無効",
        p_rxlos: "RX 信号ロス",
        pt_title: "ポート設定",
        pt_conn: "学習済みデバイス",
        pt_speed: "速度",
        pt_mtu: "MTU",
        stp_title: "スパニングツリー",
        stp_h: "RSTP, 802.1w",
        stp_version: "バージョン",
        stp_v_stp: "STP 互換",
        stp_prio: "ブリッジ優先度",
        stp_hello: "Hello [秒]",
        stp_maxage: "Max age [秒]",
        stp_fwd: "Forward delay [秒]",
        stp_txhold: "TX hold",
        stp_bridge_apply: "ブリッジ設定を適用",
        stp_off_msg: "スパニングツリーは無効です: すべてのポートが転送状態です。",
        stp_bridge: "ブリッジ",
        stp_root: "ルート",
        stp_root_self: "このブリッジがルートです",
        stp_via: "経由ポート",
        stp_cost: "パスコスト",
        stp_tc: "トポロジ変更回数",
        stp_portcfg: "ポート設定",
        stp_edge: "エッジ",
        stp_pcost: "パスコスト",
        stp_pprio: "優先度",
        stp_guard: "ガード",
        stp_filter: "BPDU フィルタ",
        stp_p2p: "ポイントツーポイント",
        stp_port_note: "エッジポートは即時に転送を開始し、トポロジ変更を発生させません。自動では BPDU を 3 秒間受信しないとエッジと見なし、BPDU を受信すると解除されます。パスコスト 0 = 自動。",
        stp_status: "ポート状態",
        stp_role: "ロール",
        stp_db: "指定ブリッジ",
        stp_dp: "指定ポート",
        stp_dc: "指定コスト",
        stp_oedge: "動作エッジ",
        stp_op2p: "動作 P2P",
        stp_s0: "無効",
        stp_s1: "ブロッキング",
        stp_s2: "学習中",
        stp_s3: "転送中",
        stp_r1: "ルート",
        stp_r2: "指定",
        stp_r3: "代替",
        stp_trip: "ガード作動",
        stp_g_none: "なし",
        stp_g_bpdu: "BPDU",
        stp_g_root: "ルート",
        stp_en_q: "スパニングツリーを有効にしますか?",
        stp_en_d: "ポートはブロック状態から始まり、転送状態になるまで最大で forward delay の 2 倍の時間がかかります。エッジポートは即時に復帰します。",
        stp_dis_q: "スパニングツリーを無効にしますか?",
        stp_dis_d: "すべてのポートが直ちに転送状態になり、ループ保護が失われます。",
        stp_cost_err: "パスコストは 0〜200000000 の範囲で指定してください",
        st_title: "ポート統計",
        st_h: "起動からの累計",
        st_txg: "TX 正常",
        st_txb: "TX 異常",
        st_rxg: "RX 正常",
        st_rxb: "RX 異常",
        st_details: "詳細",
        st_counters: "MIB カウンタ",
        st_nonzero: "0 以外のみ",
        st_autoref: "自動更新",
        st_counter: "カウンタ",
        st_value: "値",
        st_fail: "カウンタの取得に失敗しました",
        v_title: "VLAN",
        v_vid: "VID",
        v_members: "メンバー",
        v_tagged: "タグ付き",
        v_untagged: "タグ無し",
        v_pvid_on: "PVID",
        v_empty: "VLAN は設定されていません。",
        v_mgmt: "管理 VLAN",
        v_editor: "VLAN エディタ",
        v_optional: "任意",
        v_setmgmt: "管理 VLAN に設定",
        v_setmgmt_t: "この VLAN を管理 VLAN にする",
        v_member: "メンバー",
        v_pvid: "PVID",
        v_legend: "U = タグ無しメンバー、T = タグ付きメンバー、- = 非メンバー。PVID はタグ無し受信トラフィックをこの VLAN に割り当てます。",
        v_accept: "受け入れ",
        v_ing_apply: "受信モードを適用",
        v_del_t: "VLAN を削除",
        v_del_q: "VLAN {n} を削除しますか?",
        v_del_d: "ポートの PVID は再割り当てされるまで維持されます。",
        v_vid_err: "VLAN ID (1〜4094) を入力してください",
        v_loaded: "VLAN {n} を読み込みました",
        v_notfound: "VLAN {n} は存在しません (新規作成は可能です)",
        v_nomember: "メンバーポートを 1 つ以上選択してください (または VLAN を削除してください)",
        l2_title: "MAC アドレステーブル",
        l2_filter: "フィルタ...",
        l2_flush: "学習エントリを消去",
        l2_static: "静的",
        l2_learned: "学習",
        l2_loading: "読み込み中...",
        l2_failed: "読み込み失敗",
        l2_entries: "エントリ",
        l2_del_t: "エントリを削除",
        l2_flush_q: "学習した MAC エントリをすべて消去しますか?",
        m_title: "ポートミラーリング",
        m_active: "有効",
        m_monitor: "モニタポート",
        m_mirror: "ミラー",
        m_both: "両方",
        m_note: "両方 = ポートの RX と TX をモニタポートにミラーします。",
        m_none: "ミラー元ポートを 1 つ以上選択してください",
        lag_hash: "ハッシュ:",
        lag_clear_q: "LAG {n} を解除しますか?",
        lag_clear_d: "すべてのメンバーポートが通常動作に戻ります。",
        bw_title: "帯域制限",
        bw_h: "Mbit/s、0.016〜10000",
        bw_in: "入力制限",
        bw_out: "出力制限",
        bw_exceed: "超過時の動作",
        bw_fc: "フロー制御",
        bw_drop: "破棄",
        bw_in_err: "入力制限は 0.016〜10000 Mbit/s の範囲で指定してください",
        bw_out_err: "出力制限は 0.016〜10000 Mbit/s の範囲で指定してください",
        sy_network: "ネットワーク",
        sy_mode: "アドレス設定",
        sy_static: "静的",
        sy_dhcp_t: "DHCP でアドレスを取得",
        sy_services: "サービス",
        sy_igmp: "IGMP スヌーピング",
        sy_sysip: "サーバー IP",
        sy_server: "サーバー",
        sy_port: "ポート",
        sy_sesstmo: "セッションタイムアウト (秒)",
        sy_sesstmo_err: "セッションタイムアウトは 1〜65535 秒で指定してください",
        sy_services_note: "サービスの状態は起動設定を反映しています。実行時の状態は読み取れません。",
        sy_password: "管理者パスワード",
        sy_newpw: "新しいパスワード",
        sy_repeat: "再入力",
        sy_pwapply: "パスワードを変更",
        sy_pw_note: "即時に反映されます。再起動後も保持するにはフラッシュに保存してください。",
        sy_console: "コンソール",
        sy_cmd: "CLI コマンド...",
        sy_send: "送信",
        sy_startup: "起動設定",
        sy_replayed: "起動のたびに再実行されます",
        sy_reload: "フラッシュから再読み込み",
        sy_write: "フラッシュに書き込み",
        sy_unknown_note: "不明な行は書き込み前に強調表示されます。",
        sy_maint: "メンテナンス",
        sy_reboot: "スイッチを再起動",
        sy_lang: "言語",
        sy_ip_err: "IP / ネットマスク / ゲートウェイが無効です",
        sy_host_err: "ホスト名: 1〜23 文字の印字可能文字、空白と引用符は不可",
        sy_net_q: "ネットワーク設定を適用しますか?",
        sy_ip_changed: "IP を変更しました。http://{ip}/ に再接続してください",
        sy_dhcp_q: "DHCP に切り替えますか?",
        sy_sysip_err: "syslog サーバー IP が無効です",
        sy_sysport_err: "syslog ポートは 1〜65535 の範囲で指定してください",
        sy_pw_len: "パスワード: 1〜20 文字",
        sy_pw_space: "パスワードに空白は使えません",
        sy_pw_match: "パスワードが一致しません",
        sy_pw_q: "管理者パスワードを変更しますか?",
        sy_reboot_q: "スイッチを再起動しますか?",
        sy_reboot_d: "未保存の変更があり、失われます。保持したい場合は先にフラッシュに保存してください。",
        sy_rebooting: "再起動中です。約 20 秒後に再接続してください",
        sy_bytes: "バイト",
        cw_title: "この起動設定を書き込みますか?",
        cw_save_title: "実行中の設定をフラッシュに保存しますか?",
        cw_info: "{n} / 2048 バイト、起動のたびに 1 行ずつ実行されます",
        cw_toolarge: "サイズ超過: 設定セクタは最大 2048 バイトです。先に行を削除してください。",
        cw_longline: "{n} 行目は {m} バイトです: スイッチが起動時に実行できる行は 126 バイトまでで、この行は読み飛ばされるため、書き込みを拒否します。先に短くしてください。",
        cw_unknown: "既知の設定文法に含まれない行 (そのまま書き込まれます): ",
        cw_empty: "(空)",
        cw_writing: "設定を書き込み中...",
        cw_failed: "設定の書き込みに失敗しました: HTTP {n}",
        cw_verify_fail: "検証失敗: フラッシュの内容が送信内容と異なります。コマンドログは消去されていません。",
        cw_saved: "起動設定を保存し検証しました",
        cw_collect: "実行中の変更を収集中...",
        t_applied: "適用: {c}",
        t_cmds: "{n} 件のコマンドを適用しました",
        t_rejected: "コマンドが拒否されました: {c}",
        t_failed: "失敗: {c}",
        fw_title: "ファームウェア更新",
        fw_upload: "アップロード",
        fw_checking: "{f}: {n} バイト。確認中...",
        fw_size_err: "サイズが {n} です。{w} または 16384 (16 KiB のローダー) が必要です",
        fw_magic_err: "バンクヘッダ / LJMP マジックがありません: ファームウェアイメージではありません",
        fw_crc_err: "CRC16 検証に失敗しました: イメージが破損しています",
        fw_valid: "有効な RTLPlayground イメージです (サイズ、マジック、CRC16 OK)",
        fw_ldr_valid: "有効なローダーイメージです (サイズ、マジック、CRC16 OK)",
        fw_md_valid: "有効なマシンデータです (このスイッチの PoE ファームウェア、CRC16 OK)",
        fw_md_d: "その場で書き込まれ、次回の起動から使われます。スイッチの他の部分は変わりません。",
        fw_md_done: "マシンデータを書き込みました。次回の起動から使われます ✓",
        fw_ldr_d: "動作中のファームウェアがローダーを書き換え、その後スイッチをリセットします。復帰するまで電源を切らないでください。書き込み途中で止まったローダーはフラッシュライターが必要になります。",
        fw_ldr_failed: "再起動が検出されませんでした: ローダーの書き込みに失敗しました。電源を切らずに、ローダーを再度アップロードするかシリアルコンソールを確認してください。",
        fw_q: "ファームウェアをアップロードしますか?",
        fw_d: "チェックサムが正しければスイッチは自動的にリセットし、起動時にイメージを適用します (起動設定は保持されます)。復帰するまで電源を切らないでください。",
        fw_finishing: "フラッシュ書き込みを完了中... {s} 秒",
        fw_uploading: "アップロード中... {p}% / {s} 秒",
        fw_verified: "チェックサム検証済み。スイッチを再起動しています...",
        fw_rejected: "スイッチがイメージを拒否しました (チェックサム不正)。何も適用されていません",
        fw_lost: "アップロード失敗: 転送中に接続が切れました",
        fw_noreboot: "再起動が検出されませんでした。イメージは拒否された可能性が高いです。旧ファームウェアからの更新の場合は、再ログイン後にサイドバーのバージョンを確認してください。",
        fw_applied: "更新を適用しました",
        fw_done_t: "ファームウェアを更新しました",
        fw_done: "スイッチはイメージを検証し、新しいイメージで再起動しました。セッションがリセットされたため、再度ログインが必要です。",
        fw_login: "ログインへ",
        fw_rebooting: "スイッチを再起動中...",
        fw_timeout: "150 秒経ってもスイッチが復帰しません。電源 / シリアルコンソールを確認してください"
    },
    zh: {
        nav_dash: "仪表盘",
        nav_ports: "端口",
        nav_stp: "生成树",
        nav_stats: "统计",
        nav_vlan: "VLAN",
        nav_l2: "MAC 表",
        nav_mirror: "端口镜像",
        nav_lag: "LAG",
        nav_bw: "带宽限制",
        nav_system: "系统",
        nav_fw: "固件",
        nav_lldp: "LLDP",
        hdr_dirty: "未保存的更改",
        hdr_dirty_t: "运行配置与启动配置不同",
        hdr_save: "保存到 Flash",
        hdr_save_t: "将运行配置写入 Flash",
        th_auto: "跟随系统",
        th_auto_sel: "跟随系统 (Selenized)",
        sy_display: "显示",
        sy_theme: "主题",
        sy_display_note: "仅保存在此浏览器中。",
        th_light: "浅色",
        th_dark: "深色",
        th_sel_light: "Selenized 浅色",
        th_sel_dark: "Selenized 深色",
        c_port: "端口",
        c_name: "名称",
        c_link: "链路",
        c_apply: "应用",
        c_cancel: "取消",
        c_confirm: "确认",
        c_close: "关闭",
        c_refresh: "刷新",
        c_off: "关",
        c_offc: "关",
        c_down: "未连接",
        c_on: "开",
        c_auto: "自动",
        c_type: "类型",
        c_state: "状态",
        c_enabled: "启用",
        c_disable: "禁用",
        c_full: "全双工",
        c_half: "半双工",
        c_yes: "是",
        c_no: "否",
        d_system: "系统",
        d_traffic: "流量",
        d_traffic_h: "包/秒，实时",
        d_txpps: "TX pps",
        d_rxpps: "RX pps",
        d_txbad: "TX 错误",
        d_rxbad: "RX 错误",
        i_mac: "MAC",
        i_fw: "固件",
        i_built: "构建日期",
        i_uptime: "运行时间",
        i_uptime_d: "{d}天 {t}",
        i_hw: "硬件",
        i_pcb: "PCB",
        i_temp: "温度",
        i_flash: "Flash",
        p_state: "状态",
        p_disabled: "已禁用",
        p_up: "已连接",
        p_pkts: "个包",
        p_module: "模块",
        p_temp: "温度",
        p_vcc: "供电电压",
        p_txbias: "TX 偏置电流",
        p_txpower: "TX 光功率",
        p_rxpower: "RX 光功率",
        p_txfault: "TX 故障",
        p_txdis: "TX 禁用",
        p_rxlos: "RX 信号丢失",
        pt_title: "端口配置",
        pt_conn: "已学习设备",
        pt_speed: "速率",
        pt_mtu: "MTU",
        stp_title: "生成树",
        stp_h: "RSTP, 802.1w",
        stp_version: "版本",
        stp_v_stp: "STP 兼容",
        stp_prio: "网桥优先级",
        stp_hello: "Hello [秒]",
        stp_maxage: "Max age [秒]",
        stp_fwd: "Forward delay [秒]",
        stp_txhold: "TX hold",
        stp_bridge_apply: "应用网桥设置",
        stp_off_msg: "生成树已禁用: 所有端口均为转发状态。",
        stp_bridge: "网桥",
        stp_root: "根桥",
        stp_root_self: "本网桥为根桥",
        stp_via: "经由端口",
        stp_cost: "路径开销",
        stp_tc: "拓扑变更次数",
        stp_portcfg: "端口配置",
        stp_edge: "边缘端口",
        stp_pcost: "路径开销",
        stp_pprio: "优先级",
        stp_guard: "保护",
        stp_filter: "BPDU 过滤",
        stp_p2p: "点对点",
        stp_port_note: "边缘端口立即转发且不触发拓扑变更；自动模式下 3 秒未收到 BPDU 即视为边缘端口，收到 BPDU 后立即撤销。路径开销 0 = 自动。",
        stp_status: "端口状态",
        stp_role: "角色",
        stp_db: "指定网桥",
        stp_dp: "指定端口",
        stp_dc: "指定开销",
        stp_oedge: "实际边缘",
        stp_op2p: "实际 P2P",
        stp_s0: "禁用",
        stp_s1: "阻塞",
        stp_s2: "学习",
        stp_s3: "转发",
        stp_r1: "根端口",
        stp_r2: "指定端口",
        stp_r3: "替代端口",
        stp_trip: "保护触发",
        stp_g_none: "无",
        stp_g_bpdu: "BPDU",
        stp_g_root: "根桥",
        stp_en_q: "启用生成树?",
        stp_en_d: "端口将从阻塞状态开始，最多需要两倍 forward delay 的时间才能进入转发状态；边缘端口立即恢复。",
        stp_dis_q: "禁用生成树?",
        stp_dis_d: "所有端口将直接进入转发状态，失去环路保护。",
        stp_cost_err: "路径开销范围为 0-200000000",
        lldp_enabled: "LLDP 启用",
        lldp_title: "服务状态",
        lldp_portname: "名称",
        lldp_enable: "发送帧",
        stp_pcs: "路径开销",
        stp_pcs_long: "长格式 (32 bit)",
        stp_pcs_short: "短格式 (16 bit)",
        stp_bh: "STP 禁用时的 BPDU 处理方式",
        stp_bh_flood: "泛洪",
        stp_bh_filter: "过滤",
        stp_r4: "备份",
        stp_times: "最大老化时间 / 转发延迟",
        stp_proto: "协议",
        stp_nolink: "无链路",
        stp_lasttc: "上次拓扑变更",
        stp_mcheck: "重新检查",
        stp_mcheck_t: "重新发送 RST BPDU，以确认邻居是否仍需要 802.1D 兼容模式",
        stp_cnt: "计数器",
        stp_crx: "收到的 BPDU",
        stp_ctx: "发送的 BPDU",
        stp_ctrx: "收到的 TC",
        stp_cttx: "发送的 TC",
        stp_cage: "上次收到 BPDU [秒]",
        st_title: "端口统计",
        st_h: "自启动以来的累计值",
        st_txg: "TX 正常",
        st_txb: "TX 错误",
        st_rxg: "RX 正常",
        st_rxb: "RX 错误",
        st_details: "详情",
        st_counters: "MIB 计数器",
        st_nonzero: "仅显示非零",
        st_autoref: "自动刷新",
        st_counter: "计数器",
        st_value: "值",
        st_fail: "读取计数器失败",
        v_title: "VLAN",
        v_vid: "VID",
        v_members: "成员",
        v_tagged: "Tagged",
        v_untagged: "Untagged",
        v_pvid_on: "PVID",
        v_empty: "尚未配置 VLAN。",
        v_mgmt: "管理 VLAN",
        v_editor: "VLAN 编辑器",
        v_optional: "optional",
        v_setmgmt_t: "设为管理 VLAN",
        v_member: "成员",
        v_pvid: "PVID",
        v_add: "添加新 VLAN",
        v_legend: "U = untagged 成员，T = tagged 成员，- = 非成员。PVID 将入方向的未标记流量归入此 VLAN。",
        v_accept: "接受",
        v_del_t: "删除 VLAN",
        v_rename: "重命名",
        v_rename_t: "重命名 VLAN",
        v_del_q: "删除 VLAN {n}?",
        v_del_d: "端口的 PVID 在重新分配前保持不变。",
        v_vid_err: "请输入 VLAN ID (1-4094)",
        v_loaded: "已读取 VLAN {n}",
        v_notfound: "VLAN {n} 不存在 (仍可新建)",
        v_nomember: "请至少选择一个成员端口 (或改为删除该 VLAN)",
        v_mgmt_tt: "无法删除管理 VLAN，请先将管理功能分配给另一个 VLAN。",
        l2_title: "MAC 地址表",
        l2_filter: "筛选...",
        l2_flush: "清除学习条目",
        l2_static: "静态",
        l2_learned: "动态学习",
        l2_loading: "加载中...",
        l2_failed: "加载失败",
        l2_entries: "条",
        l2_del_t: "删除条目",
        l2_flush_q: "清除所有动态学习的 MAC 条目?",
        m_title: "端口镜像",
        m_active: "已启用",
        m_monitor: "镜像目的端口",
        m_mirror: "镜像",
        m_both: "双向",
        m_note: "双向 = 将该端口的 RX 和 TX 都镜像到目的端口。",
        m_none: "请至少选择一个被镜像端口",
        lag_hash: "哈希:",
        lacp_heading: "LACP (802.3ad)",
        lag_mode: "模式",
        lag_static: "静态",
        lacp_neg: "协商中",
        lacp_agg: "聚合器",
        lacp_members: "活动成员",
        lacp_actor: "本端",
        lacp_partner: "对端",
        lacp_rxstate: "接收状态",
        lacp_rx: "已接收的 LACPDU",
        lacp_psys: "对端系统",
        lag_clear_q: "清除 LAG {n}?",
        lag_clear_d: "所有成员端口恢复正常工作。",
        bw_title: "带宽限制",
        bw_h: "Mbit/s，0.016-10000",
        bw_in: "入方向限速",
        bw_out: "出方向限速",
        bw_exceed: "超限动作",
        bw_fc: "流量控制",
        bw_drop: "丢弃",
        bw_in_err: "入方向限速范围为 0.016-10000 Mbit/s",
        bw_out_err: "出方向限速范围为 0.016-10000 Mbit/s",
        sc_title: "风暴控制",
        sc_h: "按端口和流量类型设置，留空表示关闭",
        sc_bcast: "广播",
        sc_mcast: "组播",
        sc_ucast: "未知单播",
        sc_umcast: "未知组播",
        sc_err: "风暴控制阈值必须为 1-1048575 pps 或  1-10000000 kbit/s",
        sc_note: "超过对应类型限值的数据帧会在进入交换机时被丢弃。",
        sy_network: "网络",
        sy_mode: "地址分配",
        sy_static: "静态",
        sy_dhcp_t: "通过 DHCP 获取地址",
        sy_services: "服务",
        sy_igmp: "IGMP 侦听",
        sy_sysip: "服务器 IP",
        sy_server: "服务器",
        sy_port: "端口",
        sy_sesstmo: "会话超时（秒）",
        sy_sesstmo_err: "会话超时必须为 1-65535 秒",
        sy_services_note: "服务状态反映启动配置；运行时状态无法读取。",
        sy_password: "管理员密码",
        sy_newpw: "新密码",
        sy_repeat: "重复输入",
        sy_pwapply: "修改密码",
        sy_pw_note: "立即生效；如需重启后保留请保存到 Flash。",
        sy_console: "控制台",
        sy_cmd: "CLI 命令...",
        sy_send: "发送",
        sy_startup: "启动配置",
        sy_replayed: "每次启动时重新执行",
        sy_reload: "从 Flash 重新读取",
        sy_write: "写入 Flash",
        sy_unknown_note: "未知行会在写入前高亮显示。",
        sy_maint: "维护",
        sy_reboot: "重启交换机",
        sy_lang: "语言",
        sy_ip_err: "IP / 子网掩码 / 网关无效",
        sy_host_err: "主机名: 1-23 个可打印字符，不能包含空格或引号",
        sy_net_q: "应用网络设置?",
        sy_ip_changed: "IP 已更改，请访问 http://{ip}/ 重新连接",
        sy_dhcp_q: "切换到 DHCP?",
        sy_sysip_err: "syslog 服务器 IP 无效",
        sy_dns1: "DNS 服务器",
        sy_dns2: "备用 DNS 服务器",
        sy_dns_err: "DNS 服务器 IP 地址无效",
        sy_dns_dhcp: "DHCP 下发：{ip}",
        sy_time: "时间",
        sy_ntp: "NTP",
        sy_ntpsrv: "NTP 服务器",
        sy_ntpsrv_p: "主机名或 IPv4 地址",
        sy_ntpint: "轮询间隔 [分钟]",
        sy_tz: "时区",
        sy_dst: "夏令时",
        sy_dst_eu: "欧洲",
        sy_dst_us: "美国",
        sy_now: "本地时间",
        sy_sync: "上次同步时间",
        sy_ntpaddr: "服务器地址",
        sy_nosync: "未同步",
        sy_waiting: "正在等待服务器",
        sy_ago: "{n} 分钟前",
        sy_ntp_err: "请输入 NTP 服务器的主机名或 IPv4",
        sy_ntpint_err: "轮询间隔必须为 1-1440",
        sy_time_note: "交换机没有电池供电的时钟：每次重启都会丢失时间，网络连通后会从 NTP 服务器重新获取时间。保存到 Flash 以保留这些设置。",
        sy_sysport_err: "syslog 端口范围为 1-65535",
        sy_pw_len: "密码: 1-20 个字符",
        sy_pw_space: "密码不能包含空格",
        sy_pw_match: "两次输入的密码不一致",
        sy_pw_q: "修改管理员密码?",
        sy_reboot_q: "重启交换机?",
        sy_reboot_d: "存在未保存的更改，重启后将丢失。如需保留请先保存到 Flash。",
        sy_rebooting: "正在重启，约 20 秒后重新连接",
        sy_bytes: "字节",
        cw_title: "写入此启动配置?",
        cw_save_title: "将运行配置保存到 Flash?",
        cw_info: "{n} / 2048 字节，每次启动时逐行执行",
        cw_toolarge: "过大: 配置扇区最多容纳 2048 字节。请先删除部分行。",
        cw_longline: "第 {n} 行有 {m} 字节: 交换机启动时只能执行不超过 126 字节的行，该行会被跳过，因此拒绝写入。请先缩短该行。",
        cw_unknown: "不属于已知配置语法的行 (仍会写入): ",
        cw_empty: "(空)",
        cw_writing: "正在写入配置...",
        cw_failed: "配置写入失败: HTTP {n}",
        cw_verify_fail: "校验失败: Flash 内容与发送内容不一致。命令日志未清除。",
        cw_saved: "启动配置已保存并校验",
        cw_collect: "正在收集运行中的更改...",
        t_applied: "已应用: {c}",
        t_cmds: "已应用 {n} 条命令",
        t_rejected: "命令被拒绝: {c}",
        t_failed: "失败: {c}",
        fw_title: "固件升级",
        fw_upload: "上传",
        fw_checking: "{f}: {n} 字节。正在检查...",
        fw_size_err: "大小为 {n}，应为 {w} 或 16384 (16 KiB 加载程序)",
        fw_magic_err: "缺少 bank 头 / LJMP 魔数: 不是固件镜像",
        fw_crc_err: "CRC16 校验失败: 镜像已损坏",
        fw_valid: "有效的 RTLPlayground 镜像 (大小、魔数和 CRC16 正确)",
        fw_ldr_valid: "有效的加载程序镜像 (大小、魔数和 CRC16 正确)",
        fw_md_valid: "有效的机器数据 (本交换机的 PoE 固件，CRC16 正确)",
        fw_md_d: "它将被直接写入，并从下次启动起生效；交换机的其他部分不变。",
        fw_md_done: "机器数据已写入，下次启动起生效 ✓",
        fw_ldr_d: "正在运行的固件会重写加载程序，然后复位交换机。在其恢复前请勿断电：写入中断的加载程序需要用编程器修复。",
        fw_ldr_failed: "未检测到重启: 加载程序写入失败。请勿断电，重新上传加载程序或检查串口控制台。",
        fw_q: "上传固件?",
        fw_other: "此镜像并非为这台交换机（{m}）构建：其 SFP、LED 和按键引脚可能不同。只有在确认兼容的情况下才上传。",
        fw_d: "校验和正确时交换机会自动复位并在启动时应用镜像 (启动配置保留)。在其恢复前请勿断电。",
        fw_finishing: "正在完成 Flash 写入... {s} 秒",
        fw_uploading: "上传中... {p}% / {s} 秒",
        fw_verified: "校验和通过，交换机正在重启...",
        fw_rejected: "交换机拒绝了该镜像 (校验和错误)，未做任何更改",
        fw_lost: "上传失败: 传输中连接断开",
        fw_noreboot: "未检测到重启: 镜像很可能被拒绝。若从旧固件升级，请重新登录后在侧边栏确认版本。",
        fw_applied: "升级已应用",
        fw_done_t: "固件已升级",
        fw_done: "交换机已校验镜像并以新镜像重启。会话已重置，需要重新登录。",
        fw_login: "前往登录",
        fw_rebooting: "交换机正在重启...",
        fw_timeout: "150 秒后交换机仍未恢复: 请检查电源 / 串口控制台"
    },
    es: {
        nav_dash: "Panel",
        nav_ports: "Puertos",
        nav_stp: "Árbol de expansión",
        nav_stats: "Estadísticas",
        nav_vlan: "VLAN",
        nav_l2: "Tabla MAC",
        nav_mirror: "Duplicación",
        nav_lag: "LAG",
        nav_bw: "Ancho de banda",
        nav_system: "Sistema",
        nav_fw: "Firmware",
        hdr_dirty: "cambios sin guardar",
        hdr_dirty_t: "La configuración en ejecución difiere de la guardada",
        hdr_save: "Guardar en flash",
        hdr_save_t: "Persistir la configuración en ejecución a flash",
        th_auto: "Sistema",
        th_auto_sel: "Sistema (Selenized)",
        sy_display: "Pantalla",
        sy_theme: "Tema",
        sy_display_note: "Se almacena solo en este navegador.",
        th_light: "Claro",
        th_dark: "Oscuro",
        th_sel_light: "Selenized claro",
        th_sel_dark: "Selenized oscuro",
        c_port: "Puerto",
        c_name: "Nombre",
        c_link: "Enlace",
        c_apply: "Aplicar",
        c_cancel: "Cancelar",
        c_confirm: "Confirmar",
        c_close: "Cerrar",
        c_refresh: "Actualizar",
        c_off: "apagado",
        c_offc: "Apagado",
        c_down: "caído",
        c_on: "Encendido",
        c_auto: "Auto",
        c_type: "Tipo",
        c_state: "Estado",
        c_enabled: "Habilitado",
        c_disable: "Deshabilitar",
        c_full: "dúplex completo",
        c_half: "semidúplex",
        c_yes: "sí",
        c_no: "no",
        d_system: "Sistema",
        d_traffic: "Tráfico",
        d_traffic_h: "paquetes/s, en vivo",
        d_txpps: "TX pps",
        d_rxpps: "RX pps",
        d_txbad: "TX erróneos",
        d_rxbad: "RX erróneos",
        i_mac: "MAC",
        i_fw: "Firmware",
        i_built: "Compilado",
        i_uptime: "Tiempo en marcha",
        i_uptime_d: "{d} d {t}",
        i_hw: "Hardware",
        i_pcb: "PCB",
        i_flash: "Flash",
        p_state: "Estado",
        p_disabled: "deshabilitado",
        p_up: "activo",
        p_pkts: "paquetes",
        p_module: "Módulo",
        p_temp: "Temperatura",
        p_vcc: "Vcc",
        p_txbias: "Polarización TX",
        p_txpower: "Potencia TX",
        p_rxpower: "Potencia RX",
        p_txfault: "Fallo TX",
        p_txdis: "TX deshabilitado",
        p_rxlos: "Pérdida RX (LOS)",
        pt_title: "Configuración de puerto",
        pt_conn: "Dispositivos aprendidos",
        pt_speed: "Velocidad",
        pt_mtu: "MTU",
        stp_title: "Árbol de expansión",
        stp_h: "RSTP, 802.1w",
        stp_version: "Versión",
        stp_v_stp: "STP compatible",
        stp_prio: "Prioridad del puente",
        stp_hello: "Hello [s]",
        stp_maxage: "Edad máxima [s]",
        stp_fwd: "Retardo de reenvío [s]",
        stp_txhold: "Retención TX",
        stp_bridge_apply: "Aplicar ajustes del puente",
        stp_off_msg: "El árbol de expansión está deshabilitado: todos los puertos reenvían.",
        stp_bridge: "Puente",
        stp_root: "Raíz",
        stp_root_self: "este puente es la raíz",
        stp_via: "vía puerto",
        stp_cost: "coste de ruta",
        stp_tc: "cambios de topología",
        stp_portcfg: "Configuración de puertos",
        stp_edge: "Borde",
        stp_pcost: "Coste de ruta",
        stp_pprio: "Prioridad",
        stp_guard: "Protección",
        stp_filter: "Filtro BPDU",
        stp_p2p: "Punto a punto",
        stp_port_note: "Los puertos de borde reenvían inmediatamente y no causan cambios de topología; Auto trata un puerto como de borde tras 3 s sin BPDUs, y cualquier BPDU recibido lo revoca. Coste de ruta 0 = automático.",
        stp_status: "Estado de puertos",
        stp_role: "Rol",
        stp_db: "Puente designado",
        stp_dp: "Puerto designado",
        stp_dc: "Coste designado",
        stp_oedge: "Borde operativo",
        stp_op2p: "P2P operativo",
        stp_s0: "deshabilitado",
        stp_s1: "bloqueo",
        stp_s2: "aprendizaje",
        stp_s3: "reenvío",
        stp_r1: "Raíz",
        stp_r2: "Designado",
        stp_r3: "Alternativo",
        stp_trip: "protección disparada",
        stp_g_none: "Ninguno",
        stp_g_bpdu: "BPDU",
        stp_g_root: "Raíz",
        stp_en_q: "¿Habilitar el árbol de expansión?",
        stp_en_d: "Los puertos empiezan bloqueados y tardan hasta el doble del retardo de reenvío en alcanzar el reenvío; los puertos de borde se recuperan inmediatamente.",
        stp_dis_q: "¿Deshabilitar el árbol de expansión?",
        stp_dis_d: "Todos los puertos pasan directamente a reenviar; se pierde la protección contra bucles.",
        stp_cost_err: "El coste de ruta debe estar entre 0 y 200000000",
        st_title: "Estadísticas de puertos",
        st_h: "totales desde el arranque",
        st_txg: "TX correctos",
        st_txb: "TX erróneos",
        st_rxg: "RX correctos",
        st_rxb: "RX erróneos",
        st_details: "Detalles",
        st_counters: "Contadores MIB",
        st_nonzero: "solo no nulos",
        st_autoref: "actualización automática",
        st_counter: "Contador",
        st_value: "Valor",
        st_fail: "error al cargar contadores",
        v_title: "VLANs",
        v_vid: "VID",
        v_members: "Miembros",
        v_tagged: "Etiquetados",
        v_untagged: "Sin etiquetar",
        v_pvid_on: "PVID activo",
        v_empty: "No hay VLANs configuradas.",
        v_mgmt: "VLAN de gestión",
        v_editor: "Editor de VLANs",
        v_optional: "opcional",
        v_setmgmt: "Establecer como VLAN de gestión",
        v_setmgmt_t: "Hacer esta la VLAN de gestión",
        v_member: "Miembro",
        v_pvid: "PVID",
        v_legend: "U = miembro sin etiquetar, T = miembro etiquetado, - = no miembro. PVID asigna esta VLAN al tráfico entrante sin etiquetar.",
        v_accept: "Aceptar",
        v_ing_apply: "Aplicar modos de entrada",
        v_del_t: "Eliminar VLAN",
        v_del_q: "¿Eliminar VLAN {n}?",
        v_del_d: "Los puertos conservan su PVID hasta que se reasignen.",
        v_vid_err: "Introduce un ID de VLAN (1-4094)",
        v_loaded: "VLAN {n} cargada",
        v_notfound: "VLAN {n} no encontrada (aún se puede crear una nueva)",
        v_nomember: "Selecciona al menos un puerto miembro (o elimina la VLAN)",
        l2_title: "Tabla de direcciones MAC",
        l2_filter: "filtrar...",
        l2_flush: "Vaciar entradas aprendidas",
        l2_static: "estática",
        l2_learned: "aprendida",
        l2_loading: "cargando...",
        l2_failed: "error de carga",
        l2_entries: "entradas",
        l2_del_t: "Eliminar entrada",
        l2_flush_q: "¿Vaciar todas las entradas MAC aprendidas?",
        m_title: "Duplicación de puertos",
        m_active: "activa",
        m_monitor: "Puerto monitor",
        m_mirror: "Duplicar",
        m_both: "Ambos",
        m_note: "Ambos = duplica RX y TX del puerto al puerto monitor.",
        m_none: "Selecciona al menos un puerto duplicado",
        lag_hash: "Hash:",
        lag_clear_q: "¿Borrar LAG {n}?",
        lag_clear_d: "Todos los puertos miembros vuelven a la operación normal.",
        bw_title: "Límites de ancho de banda",
        bw_h: "Mbit/s, 0.016-10000",
        bw_in: "Límite de entrada",
        bw_out: "Límite de salida",
        bw_exceed: "Al excederse",
        bw_fc: "Control de flujo",
        bw_drop: "Descartar",
        bw_in_err: "El límite de entrada debe estar entre 0.016 y 10000 Mbit/s",
        bw_out_err: "El límite de salida debe estar entre 0.016 y 10000 Mbit/s",
        sy_network: "Red",
        sy_mode: "Direccionamiento",
        sy_static: "Estática",
        sy_dhcp_t: "Solicitar dirección mediante DHCP",
        sy_services: "Servicios",
        sy_igmp: "IGMP snooping",
        sy_sysip: "IP del servidor",
        sy_server: "Servidor",
        sy_port: "Puerto",
        sy_sesstmo: "Tiempo de sesión (s)",
        sy_sesstmo_err: "El tiempo de sesión debe estar entre 1 y 65535 s",
        sy_services_note: "El estado del servicio refleja la configuración de inicio; el estado en ejecución no es legible.",
        sy_password: "Contraseña de administrador",
        sy_newpw: "Nueva contraseña",
        sy_repeat: "Repetir",
        sy_pwapply: "Cambiar contraseña",
        sy_pw_note: "Toma efecto inmediatamente; guarda en flash para conservarla tras el reinicio.",
        sy_console: "Consola",
        sy_cmd: "Comando de CLI...",
        sy_send: "Enviar",
        sy_startup: "Configuración de inicio",
        sy_replayed: "reaplicada en cada arranque",
        sy_reload: "Recargar desde flash",
        sy_write: "Escribir en flash",
        sy_unknown_note: "Las líneas desconocidas se resaltan antes de escribir.",
        sy_maint: "Mantenimiento",
        sy_reboot: "Reiniciar switch",
        sy_lang: "Idioma",
        sy_ip_err: "IP / máscara / puerta de enlace no válidos",
        sy_host_err: "Nombre de host: 1-23 caracteres imprimibles, sin espacios ni comillas",
        sy_net_q: "¿Aplicar ajustes de red?",
        sy_ip_changed: "IP cambiada, reconecta en http://{ip}/",
        sy_dhcp_q: "¿Cambiar a DHCP?",
        sy_sysip_err: "IP del servidor syslog no válida",
        sy_sysport_err: "El puerto de syslog debe estar entre 1 y 65535",
        sy_pw_len: "Contraseña: 1-20 caracteres",
        sy_pw_space: "La contraseña no puede contener espacios",
        sy_pw_match: "Las contraseñas no coinciden",
        sy_pw_q: "¿Cambiar la contraseña de administrador?",
        sy_reboot_q: "¿Reiniciar el switch?",
        sy_reboot_d: "Hay cambios SIN GUARDAR y se perderán. Guarda en flash primero si quieres conservarlos.",
        sy_rebooting: "Reiniciando, reconecta en unos 20 s",
        sy_bytes: "bytes",
        cw_title: "¿Escribir esta configuración de inicio?",
        cw_save_title: "¿Guardar la configuración en ejecución en flash?",
        cw_info: "{n} / 2048 bytes, reaplicada línea a línea en cada arranque",
        cw_toolarge: "Demasiado grande: el sector de configuración admite como máximo 2048 bytes. Elimina líneas primero.",
        cw_unknown: "No está en la gramática de configuración conocida (aun así se escribirá): ",
        cw_empty: "(vacío)",
        cw_writing: "Escribiendo configuración...",
        cw_failed: "error al escribir la configuración: HTTP {n}",
        cw_verify_fail: "Verificación fallida: el contenido de la flash difiere de lo enviado. El registro de comandos NO se ha borrado.",
        cw_saved: "Configuración de inicio guardada y verificada",
        cw_collect: "Recopilando cambios en ejecución...",
        t_applied: "Aplicado: {c}",
        t_cmds: "{n} comando(s) aplicado(s)",
        t_rejected: "comando rechazado: {c}",
        t_failed: "fallo: {c}",
        fw_title: "Actualización de firmware",
        fw_upload: "Subir",
        fw_checking: "{f}: {n} bytes. Comprobando...",
        fw_size_err: "el tamaño es {n}, se esperaban {w} o 16384 (cargador de 16 KiB)",
        fw_magic_err: "falta la cabecera de banco / magia LJMP: no es una imagen de firmware",
        fw_crc_err: "comprobación CRC16 fallida: imagen corrupta",
        fw_valid: "imagen de RTLPlayground válida (tamaño, magia y CRC16 correctos)",
        fw_ldr_valid: "imagen de cargador válida (tamaño, magia y CRC16 correctos)",
        fw_md_valid: "datos de máquina válidos (firmware PoE de este switch, CRC16 correcto)",
        fw_md_d: "Se escriben en su sitio y se usan desde el próximo arranque; nada más cambia en el switch.",
        fw_md_done: "datos de máquina escritos, se usan desde el próximo arranque ✓",
        fw_ldr_d: "El firmware en ejecución reescribe el cargador y luego reinicia el switch. No lo apagues hasta que vuelva: un cargador interrumpido a mitad de escritura necesita un programador de flash.",
        fw_ldr_failed: "no se detectó reinicio: falló la escritura del cargador. No apagues el switch, vuelve a subir el cargador o revisa la consola serie.",
        fw_q: "¿Subir firmware?",
        fw_d: "Si la suma de comprobación es correcta, el switch se reinicia y aplica la imagen durante el arranque (la configuración de inicio se conserva). No lo apagues hasta que vuelva.",
        fw_finishing: "terminando escritura en flash... {s} s",
        fw_uploading: "subiendo... {p}% / {s} s",
        fw_verified: "suma de comprobación verificada, el switch se está reiniciando...",
        fw_rejected: "el switch rechazó la imagen (suma de comprobación incorrecta), no se aplicó nada",
        fw_lost: "error de subida: conexión perdida durante la transferencia",
        fw_noreboot: "no se detectó reinicio: lo más probable es que la imagen fuera rechazada. Si actualizas desde un firmware antiguo, verifica la versión en la barra lateral tras volver a iniciar sesión.",
        fw_applied: "actualización aplicada",
        fw_done_t: "Firmware actualizado",
        fw_done: "El switch verificó la imagen y se reinició con ella. La sesión se restableció, así que se te pedirá que vuelvas a iniciar sesión.",
        fw_login: "Ir al inicio de sesión",
        fw_rebooting: "el switch se está reiniciando...",
        fw_timeout: "el switch no ha vuelto tras 150 s: comprueba la alimentación / consola serie",
        nav_lldp: "LLDP",
        i_temp: "Temperatura",
        lldp_enabled: "LLDP activado",
        lldp_title: "Estado del servicio",
        lldp_portname: "Nombre",
        lldp_enable: "Enviar tramas",
        stp_pcs: "Coste de ruta",
        stp_pcs_long: "Largo (32 bits)",
        stp_pcs_short: "Corto (16 bits)",
        stp_bh: "BPDU con STP desactivado",
        stp_bh_flood: "Inundar",
        stp_bh_filter: "Filtrar",
        stp_r4: "Respaldo",
        stp_times: "edad máxima / retardo de reenvío",
        stp_proto: "Protocolo",
        stp_nolink: "sin enlace",
        stp_lasttc: "último",
        stp_mcheck: "Comprobar",
        stp_mcheck_t: "Reenviar BPDUs RST para saber si el vecino sigue necesitando 802.1D",
        stp_cnt: "Contadores",
        stp_crx: "BPDUs recibidas",
        stp_ctx: "BPDUs enviadas",
        stp_ctrx: "TC recibidas",
        stp_cttx: "TC enviadas",
        stp_cage: "Última BPDU [s]",
        v_add: "Añadir una VLAN",
        v_rename: "Renombrar",
        v_rename_t: "Renombrar VLAN",
        v_mgmt_tt: "La VLAN de gestión no se puede borrar, primero hay que asignar la gestión a otra VLAN.",
        lacp_heading: "LACP (802.3ad)",
        lag_mode: "Modo",
        lag_static: "Estático",
        lacp_neg: "negociando",
        lacp_agg: "agregador",
        lacp_members: "miembros activos",
        lacp_actor: "Local",
        lacp_partner: "Remoto",
        lacp_rxstate: "Estado RX",
        lacp_rx: "LACPDUs recibidas",
        lacp_psys: "Sistema remoto",
        sc_title: "Control de tormentas",
        sc_h: "por puerto y tipo de tráfico, vacío = desactivado",
        sc_bcast: "Difusión",
        sc_mcast: "Multidifusión",
        sc_ucast: "Unidifusión desconocida",
        sc_umcast: "Multidifusión desconocida",
        sc_err: "El límite de tormenta debe ser 1-1048575 pps o 1-10000000 kbit/s",
        sc_note: "Las tramas de un tipo por encima de su límite se descartan a la entrada del switch.",
        sy_dns1: "Servidor DNS",
        sy_dns2: "DNS secundario",
        sy_dns_err: "IP del servidor DNS no válida",
        sy_dns_dhcp: "desde DHCP: {ip}",
        sy_time: "Hora",
        sy_ntp: "NTP",
        sy_ntpsrv: "Servidor NTP",
        sy_ntpsrv_p: "nombre de host o dirección IPv4",
        sy_ntpint: "Intervalo de consulta [min]",
        sy_tz: "Zona horaria",
        sy_dst: "Horario de verano",
        sy_dst_eu: "Europa",
        sy_dst_us: "Estados Unidos",
        sy_now: "Hora local",
        sy_sync: "Última sincronización",
        sy_ntpaddr: "Dirección del servidor",
        sy_nosync: "sin sincronizar",
        sy_waiting: "esperando al servidor",
        sy_ago: "hace {n} min",
        sy_ntp_err: "Introduce el nombre de host o la dirección IPv4 del servidor NTP",
        sy_ntpint_err: "El intervalo de consulta debe ser 1-1440",
        sy_time_note: "El switch no tiene reloj con batería: la hora se pierde en cada arranque y se vuelve a fijar desde el servidor NTP cuando la red está lista. Guarda en flash para conservar los ajustes.",
        cw_longline: "La línea {n} mide {m} bytes: el switch solo puede ejecutar líneas de hasta 126 bytes y la saltaría al arrancar, así que se rechaza la escritura. Acórtala primero.",
        fw_other: "Esta imagen no se compiló para este switch ({m}): los pines de SFP, LEDs y botones pueden no coincidir. Súbela solo si estás seguro."
    },
    fr: {
        nav_dash: "Tableau de bord",
        nav_ports: "Ports",
        nav_stp: "Arborescence",
        nav_stats: "Statistiques",
        nav_vlan: "LANs virtuels",
        nav_l2: "Table MAC",
        nav_mirror: "Mise en mirroir",
        nav_lag: "Aggrégation",
        nav_bw: "Bande passante",
        nav_system: "Système",
        nav_fw: "Micro logiciel",
        nav_lldp: "Couche de Liaison",
        hdr_dirty: "Changements non sauvegardés",
        hdr_dirty_t: "La configuration utilisée diffère de celle au démarrage",
        hdr_save: "Sauvegarder en flash",
        hdr_save_t: "Maintenir la configuration actuelle en flash",
        th_auto: "Système",
        th_auto_sel: "Système (sélénisé)",
        sy_display: "Affichage",
        sy_theme: "Thème",
        sy_display_note: "Enregistré seulement dans ce navigateur.",
        th_light: "Clair",
        th_dark: "Sombre",
        th_sel_light: "Clair (sélénisé)",
        th_sel_dark: "Sombre (sélénisé)",
        c_port: "Port",
        c_name: "Nom",
        c_link: "Lien",
        c_apply: "Appliquer",
        c_cancel: "Annuler",
        c_confirm: "Confirmer",
        c_close: "Fermer",
        c_refresh: "Raffraichir",
        c_off: "arrêt",
        c_offc: "Arrêt",
        c_down: "arrêt",
        c_on: "Marche",
        c_auto: "Auto",
        c_type: "Type",
        c_state: "Etat",
        c_enabled: "Activé",
        c_disable: "Désactivé",
        c_full: "full",
        c_half: "half",
        c_yes: "oui",
        c_no: "non",
        d_system: "Système",
        d_traffic: "Trafic",
        d_traffic_h: "paquets/s (en direct)",
        d_txpps: "TX pps",
        d_rxpps: "RX pps",
        d_txbad: "TX KO",
        d_rxbad: "RX KO",
        i_mac: "Adresse MAC",
        i_fw: "Micro logiciel",
        i_built: "Compilé le",
        i_uptime: "Temps de fonctionnement",
        i_uptime_d: "{d} j {t}",
        i_hw: "Matériel",
        i_pcb: "PCB",
        i_temp: "Température",
        i_flash: "Taille Flash",
        p_state: "Etat",
        p_disabled: "Inactif",
        p_up: "Actif",
        p_pkts: "paquets",
        p_module: "Module",
        p_temp: "Température",
        p_vcc: "Vcc",
        p_txbias: "TX biais",
        p_txpower: "TX puissance",
        p_rxpower: "RX puissance",
        p_txfault: "TX erreur",
        p_txdis: "TX désactivé",
        p_rxlos: "RX pertes de signaux",
        pt_title: "Configuration du port",
        pt_conn: "Appareils appris",
        pt_speed: "Vitesse",
        pt_mtu: "MTU",
        stp_title: "Arborescence",
        stp_h: "RSTP, 802.1w",
        stp_version: "Protocole",
        stp_v_stp: "Compatible STP",
        stp_prio: "Priorité",
        stp_hello: "Bonjour [s]",
        stp_maxage: "Age max [s]",
        stp_fwd: "Délai de transmission [s]",
        stp_txhold: "TX en attente",
        stp_bridge_apply: "Appliquer les paramètres du pont",
        stp_off_msg: "Arborescence désactivée: Tous les ports sont retransmis.",
        stp_bridge: "Pont",
        stp_root: "Racine",
        stp_root_self: "Ce pont est la racine",
        stp_via: "via le port",
        stp_cost: "coût du chemin",
        stp_tc: "changements de topologie",
        stp_portcfg: "Configuration du port",
        stp_edge: "Port terminal",
        stp_pcost: "Coût du chemin",
        stp_pprio: "Priorité",
        stp_guard: "Protection",
        stp_filter: "Filtre BPDU",
        stp_p2p: "Point-à-point",
        stp_port_note: "Les ports en bordure sont transmis immediatemment et ne change pas la topologie; Auto traite le port comme une bordure après 3s sans BPDUs, et chaque BPDU reçu le révoque. Le coût du chemin  0 = automatique.",
        stp_status: "Statut du port",
        stp_role: "Rôle",
        stp_db: "Pont désigné",
        stp_dp: "Port désigné",
        stp_dc: "Coût désigné",
        stp_oedge: "Oper. terminales",
        stp_op2p: "Oper. en P2P",
        stp_s0: "désactivé",
        stp_s1: "bloquage",
        stp_s2: "apprentissage",
        stp_s3: "retransmission",
        stp_r1: "Racine",
        stp_r2: "Designé",
        stp_r3: "Alternatif",
        stp_trip: "protection déclenchée",
        stp_g_none: "Aucun",
        stp_g_bpdu: "BPDU",
        stp_g_root: "Racine",
        stp_en_q: "Activer l'arbrescence?",
        stp_en_d: "Les ports sont d'abord bloqués et cela prend deux fois le délai de retransmission pour atteindre l'état de retransmission; les ports en bordures recouvrent immédiatement leur état.",
        stp_dis_q: "Désactiver l'arborescence ?",
        stp_dis_d: "Tous les ports vont être retransmis; la protection de boucles est perdue.",
        stp_cost_err: "Le coût du chemin doit être compris entre 0 et 200000000",
        lldp_enabled: "Activer LLDP",
        lldp_title: "Protocole de découverte de la couche de liason (LLDP)",
        lldp_portname: "Nom",
        lldp_enable: "Envoi de trames",
        st_title: "Statiqtique du port",
        st_h: "cumul depuis le démarrage",
        st_txg: "TX OK",
        st_txb: "TX KO",
        st_rxg: "RX OK",
        st_rxb: "RX KO",
        st_details: "Détails",
        st_counters: "Compteur MIB",
        st_nonzero: "non-zero seulement",
        st_autoref: "rafraichissement automatique",
        st_counter: "Compteur",
        st_value: "Valeur",
        st_fail: "Erreur au chargement des compteurs",
        v_title: "VLANs",
        v_vid: "VID",
        v_members: "Membres",
        v_tagged: "Tagué",
        v_untagged: "Détagué",
        v_pvid_on: "PVID on",
        v_empty: "Aucun VLANs configuré.",
        v_mgmt: "VLAN de management",
        v_editor: "Editeur de VLAN",
        v_optional: "optionel",
        v_setmgmt: "Mette en VLAN de management",
        v_setmgmt_t: "Mettre celui-ci en VLAN de management",
        v_member: "Membre",
        v_pvid: "PVID",
        v_legend: "U = Membre détagué (UnTagged), T = membre tagué (Tagged), - = non membre. PVID (Port VLan ID) assigne ce VLAN au trafic entrant non tagué.",
        v_accept: "Accepté",
        v_ing_apply: "Appliquer les filtes",
        v_del_t: "Supprimer le VLAN",
        v_del_q: "Supprimer le VLAN {n}?",
        v_del_d: "Les ports conservent leur PVID tant qu'ils ne sont pas réassignés.",
        v_vid_err: "Entrez un identifiant VLAN (1-4094)",
        v_loaded: "VLAN chargés {n}",
        v_notfound: "VLAN {n} non trouvé (un nouveau peut toutefois être créé)",
        v_nomember: "Selectionnez au moins un port membre (ou supprimez le VLAN)",
        l2_title: "Table des adresses MAC",
        l2_filter: "filtre...",
        l2_flush: "Effacer les entrées apprises",
        l2_static: "static",
        l2_learned: "appris",
        l2_loading: "chargement...",
        l2_failed: "chargement non réussi",
        l2_entries: "entrées",
        l2_del_t: "Supprimer l'entrée",
        l2_flush_q: "Effacer toutes les entrées MAC apprises?",
        m_title: "Duplication de ports",
        m_active: "actif",
        m_monitor: "Port de supervision",
        m_mirror: "Mirroir",
        m_both: "RX&TX",
        m_note: "RX&TX = duplique RX et TX du port vers le port de supervision.",
        m_none: "Selectionnez au moins un port à dupliquer",
        lag_hash: "Hachage:",
        lag_clear_q: "Effacer l'agrégation {n}?",
        lag_clear_d: "Tous les ports membres vont redevenir normaux.",
        bw_title: "Limitations de bande passante",
        bw_h: "Mbit/s, 0.016-10000",
        bw_in: "Limite entrante",
        bw_out: "Limite sortante",
        bw_exceed: "Lorsque dépassé",
        bw_fc: "Limiter",
        bw_drop: "Rejetter",
        bw_in_err: "La limite entrante doit être comprise entre 0.016 et 10000 Mbit/s",
        bw_out_err: "La limite sortante doit être comprise entre 0.016 et 10000 Mbit/s",
        sc_title: "Contrôle des tempêtes",
        sc_h: "par port et par type de diffusion (générale ou groupée), vide = inactif",
        sc_bcast: "Générale (Broadcast)",
        sc_mcast: "Groupée (Multicast)",
        sc_ucast: "Unicast inconnu",
        sc_umcast: "Multicast inconnu",
        sc_err: " La limite de tempête doit être comprise entre 1 à 1048575 pps ou 1 à 10000000 kbit/s",
        sc_note: "Les trames de chacun des types ci-dessus dépassant les limites sont abandonnées lorsqu'elles arrivent sur le communtateur.",
        sy_network: "Réseau",
        sy_mode: "Adressage",
        sy_static: "Statique",
        sy_dhcp_t: "Aquérir l'adresse via le protocole DHCP",
        sy_services: "Services",
        sy_igmp: "Surveillance IGMP",
        sy_sysip: "IP du serveur...",
        sy_server: "Serveur",
        sy_port: "Port",
        sy_sesstmo: "Temps mort de session (s)",
        sy_sesstmo_err: "Le temps morts de session doit être compris en 1-65535 s",
        sy_services_note: "L'état de service dépend de la configuration de démarrage; Le temps de fonctionnement n'est pas consultable.",
        sy_password: "Gestion de l'accès",
        sy_newpw: "Mot de passe",
        sy_repeat: "Confirmation",
        sy_pwapply: "Changer le mot de passe",
        sy_pw_note: "Prends effet immédiatement; Sauvegardez en flash afin de conserver la configuration lors du redémarrage.",
        sy_console: "Console",
        sy_cmd: "Ligne de commande...",
        sy_send: "Envoyer",
        sy_startup: "Configuration de démarrage",
        sy_replayed: "Rejouée à chaque redémarrage",
        sy_reload: "Recharger depuis la mémoire flash",
        sy_write: "Ecrire en mémoire flash",
        sy_unknown_note: "Les lignes incorrectes sont mises en évidence avant l'écriture.",
        sy_maint: "Maintenance",
        sy_reboot: "Redémarre le commutateur",
        sy_lang: "Langage",
        sy_ip_err: "Erreur d'IP / masque réseau / passerelle",
        sy_host_err: "Nom d'hôte : 1 à 23 caractères, sans espace ni apostrophes",
        sy_net_q: "Appliquer les paramètres réseau?",
        sy_ip_changed: "L'adresse IP a changée, reconnectez-vous à http://{ip}/",
        sy_dhcp_q: "Passer en DHCP?",
        sy_sysip_err: "Adresse IP du serveur Syslog invalide",
        sy_dns1: "DNS principal",
        sy_dns2: "DNS secondaire",
        sy_dns_err: "Adresse IP du DNS invalide",
        sy_dns_dhcp: "émis par le DHCP: {ip}",
        sy_time: "Temps",
        sy_ntp: "NTP",
        sy_ntpsrv: "Serveur NTP",
        sy_ntpsrv_p: "Nom d'hôte ou adresse IPv4",
        sy_ntpint: "intervalle de scrutation [min]",
        sy_tz: "Fuseau horaire",
        sy_dst: "Heure d'été",
        sy_dst_eu: "Europe",
        sy_dst_us: "Etats-Unis",
        sy_now: "Heure locale",
        sy_sync: "Dernière synchronisation",
        sy_ntpaddr: "Adresse du serveur",
        sy_nosync: "Désynchronisé",
        sy_waiting: "En attente du serveur",
        sy_ago: "depuis {n} minutes",
        sy_ntp_err: "Entrez le nom d'hôte ou l'adresse IPv4 du serveur NTP",
        sy_ntpint_err: "L'intervalle de scrutation doit être compris entre 1 et 1440",
        sy_time_note: "Le commutateur n'a pas d'horloge sauvegardée par batterie: L'heure est donc perdue à chaque redémarrage et doit être récupérée grâce au serveur NTP une fois le réseau fonctionnel. Sauvegardez en mémoire flash pour enregistrer les paramètres.",
        sy_sysport_err: "Le port Syslog doit être compris entre 1 et 65535",
        sy_pw_len: "Mot de passe : 1 à 20 caractères",
        sy_pw_space: "Le mot de passe ne doit pas contenir d'espaces",
        sy_pw_match: "Les mots de passe ne correspondent pas",
        sy_pw_q: "Changer le mot de passe d'administration?",
        sy_reboot_q: "Redémarrer le commutateur ?",
        sy_reboot_d: "Il y a des paramètres NON SAUVEGARDES qui vont être perdus. Sauvegardez-les au préalable en mémoire flash si vous souhaitez les conserver.",
        sy_rebooting: "Redémarrage, reconnectez-vous d'ici 20s",
        sy_bytes: "bytes",
        cw_title: "Enregistrer cette configuration de démarrage?",
        cw_save_title: "Enregistrer la configuration active en mémoire flash?",
        cw_info: "{n} / 2048 bytes, qui seront rejouées ligne par ligne à chaque redémarrage",
        cw_toolarge: "Trop grand : La quota du secteur de démarrage ne peut excéder 2048 bytes. Supprimez des lignes.",
        cw_longline: "La ligne {n} est longue de {m} bytes: Le commutateur ne peut exécuter des lignes de plus de 126 bytes et les omettra donc au démarragee, l'écriture est refusé. Raccourcissez les d'abord.",
        cw_unknown: "Ne respecte pas la grammaire de configuration (sera néanmoins sauvegardé): ",
        cw_empty: "(vide)",
        cw_writing: "Configuration enregistrée...",
        cw_failed: "Echec de l'enregistrement de la configuration: HTTP {n}",
        cw_verify_fail: "Verification erronée: La mémoire flash présente des différences par rapport à ce qui a été transmis. Le log de commande ne sera pas éffacé.",
        cw_saved: "Configuration de démarrage sauvegardée et vérifiée",
        cw_collect: "Récolement des modification des paramètres courants...",
        t_applied: "Appliqué: {c}",
        t_cmds: "{n} commande(s) appliquée(s)",
        t_rejected: "commandes rejetées: {c}",
        t_failed: "échecs: {c}",
        fw_title: "Mise à jour du micro-logiciel",
        fw_upload: "Charger",
        fw_checking: "{f}: {n} bytes. Vérification...",
        fw_size_err: "la taille est {n} alors que nous attendons {w} ou 16384 (chargeur de 16 KiB)",
        fw_magic_err: "Entête de banque de donnée manquant / contrôle LJMP: ce n'est pas une image de firmware",
        fw_crc_err: "Somme de contrôle erronée: image corrompue",
        fw_valid: "Image RTLPlayground valide (taille, LJMP et somme de contrôle OK)",
        fw_ldr_valid: "image de chargeur valide (taille, LJMP et somme de contrôle OK)",
        fw_md_valid: "données machine valides (micrologiciel PoE de ce commutateur, CRC16 OK)",
        fw_md_d: "Elles sont écrites sur place et utilisées dès le prochain démarrage ; rien d'autre ne change sur le commutateur.",
        fw_md_done: "données machine écrites, utilisées dès le prochain démarrage ✓",
        fw_ldr_d: "Le firmware en cours réécrit le chargeur puis redémarre le commutateur. Ne coupez pas l'alimentation avant son retour : un chargeur interrompu pendant l'écriture nécessite un programmateur de flash.",
        fw_ldr_failed: "Pas de redémarrage détecté : l'écriture du chargeur a échoué. Ne coupez pas l'alimentation, chargez à nouveau le chargeur ou vérifiez la console série.",
        fw_q: "Chargement du firmware?",
        fw_other: "Cette image n'a pas été prévue pour ce commutateur({m}): La puce, les LED et les boouton peuvent être différents. Poursuivez seulement si vous êtes sûrs de vous.",
        fw_d: "Si la somme de contrôle est correcte, le commutateur va redémarrer tout seul et charger la nouvelle image lors du démarrage (la configuration est préservée). Ne pas éteindre l'appareil avant que les voyants ne s'éteignent.",
        fw_finishing: "Fin d'écriture en mémoire flash... {s} s",
        fw_uploading: "Chargement... {p}% / {s} s",
        fw_verified: "Somme de contrôle vérifiée, le commutateur redémarre...",
        fw_rejected: "L'image est rejettée en raison d'une somme de contrôle incorrecte, rien n'a été appliqué",
        fw_lost: "Erreur lors du chargement: connection perdue à mi-transfert",
        fw_noreboot: "Pas de redemarrage détecté: l'image a certainement été refusée. Si vous chargesz un ancien firmware, verifiez la version affichée après l'écran de connection.",
        fw_applied: "Mise à jour effectuée",
        fw_done_t: "Firmware mis à jour",
        fw_done: "Le commutateur a vérifié l'image et vient de redémarrer. La session a été remise à zero, vous devez vous reconnecter.",
        fw_login: "Vers l'écran de connection",
        fw_rebooting: "Le commutateur redémarre...",
        fw_timeout: "Le commutateur n'est plus joignable depuis 150 s: verifiez l'alimentation ou utilisez la console série"
    },
    de: {
        nav_dash: "Übersicht",
        nav_ports: "Ports",
        nav_stp: "Spanning Tree",
        nav_stats: "Statistik",
        nav_vlan: "VLANs",
        nav_l2: "MAC-Tabelle",
        nav_mirror: "Spiegelung",
        nav_lag: "LAG",
        nav_bw: "Bandbreite",
        nav_system: "System",
        nav_fw: "Firmware",
        hdr_dirty: "ungespeicherte Änderungen",
        hdr_dirty_t: "Laufende Konfiguration weicht von der Startkonfiguration ab",
        hdr_save: "Im Flash speichern",
        hdr_save_t: "Laufende Konfiguration dauerhaft im Flash speichern",
        th_auto: "System",
        th_auto_sel: "System (Selenized)",
        sy_display: "Anzeige",
        sy_theme: "Farbschema",
        sy_display_note: "Wird nur in diesem Browser gespeichert.",
        th_light: "Hell",
        th_dark: "Dunkel",
        th_sel_light: "Selenized Hell",
        th_sel_dark: "Selenized Dunkel",
        c_port: "Port",
        c_name: "Name",
        c_link: "Link",
        c_apply: "Übernehmen",
        c_cancel: "Abbrechen",
        c_confirm: "Bestätigen",
        c_close: "Schliessen",
        c_refresh: "Aktualisieren",
        c_off: "aus",
        c_offc: "Aus",
        c_down: "getrennt",
        c_on: "Ein",
        c_auto: "Auto",
        c_type: "Typ",
        c_state: "Zustand",
        c_enabled: "Aktiviert",
        c_disable: "Deaktivieren",
        c_full: "voll",
        c_half: "halb",
        c_yes: "ja",
        c_no: "nein",
        d_system: "System",
        d_traffic: "Datenverkehr",
        d_traffic_h: "Pakete/s, live",
        d_txpps: "TX Pakete/s",
        d_rxpps: "RX Pakete/s",
        d_txbad: "TX fehlerhaft",
        d_rxbad: "RX fehlerhaft",
        i_mac: "MAC",
        i_fw: "Firmware",
        i_built: "Erstellt",
        i_uptime: "Betriebszeit",
        i_uptime_d: "{d} T {t}",
        i_hw: "Hardware",
        i_pcb: "PCB",
        i_temp: "Temperatur",
        i_flash: "Flash",
        p_state: "Zustand",
        p_disabled: "deaktiviert",
        p_up: "verbunden",
        p_pkts: "Pakete",
        p_module: "Modul",
        p_temp: "Temperatur",
        p_vcc: "Vcc",
        p_txbias: "TX-Bias",
        p_txpower: "Sendeleistung",
        p_rxpower: "Empfangsleistung",
        p_txfault: "TX-Fehler",
        p_txdis: "TX deaktiviert",
        p_rxlos: "RX LOS",
        pt_title: "Port-Konfiguration",
        pt_conn: "Gelernte Geräte",
        pt_speed: "Geschwindigkeit",
        pt_mtu: "MTU",
        stp_title: "Spanning Tree",
        stp_h: "RSTP, 802.1w",
        stp_version: "Version",
        stp_v_stp: "STP-kompatibel",
        stp_prio: "Bridge-Priorität",
        stp_hello: "Hello [s]",
        stp_maxage: "Max. Alter [s]",
        stp_fwd: "Forward Delay [s]",
        stp_txhold: "TX Hold",
        stp_bridge_apply: "Bridge-Einstellungen übernehmen",
        stp_off_msg: "Spanning Tree ist deaktiviert: Alle Ports leiten weiter.",
        stp_bridge: "Bridge",
        stp_root: "Root",
        stp_root_self: "diese Bridge ist Root",
        stp_via: "über Port",
        stp_cost: "Pfadkosten",
        stp_tc: "Topologieänderungen",
        stp_portcfg: "Port-Konfiguration",
        stp_edge: "Edge",
        stp_pcost: "Pfadkosten",
        stp_pprio: "Priorität",
        stp_guard: "Guard",
        stp_filter: "BPDU-Filter",
        stp_p2p: "Punkt-zu-Punkt",
        stp_port_note: "Edge-Ports leiten sofort weiter und lösen keine Topologieänderung aus; bei Auto gilt ein Port nach 3 s ohne BPDUs als Edge, jede empfangene BPDU hebt das wieder auf. Pfadkosten 0 = automatisch.",
        stp_status: "Port-Status",
        stp_role: "Rolle",
        stp_db: "Designated Bridge",
        stp_dp: "Designated Port",
        stp_dc: "Designated Cost",
        stp_oedge: "Edge (aktiv)",
        stp_op2p: "P2P (aktiv)",
        stp_s0: "deaktiviert",
        stp_s1: "blockiert",
        stp_s2: "lernt",
        stp_s3: "leitet weiter",
        stp_r1: "Root",
        stp_r2: "Designated",
        stp_r3: "Alternate",
        stp_trip: "Guard ausgelöst",
        stp_g_none: "Keiner",
        stp_g_bpdu: "BPDU",
        stp_g_root: "Root",
        stp_en_q: "Spanning Tree aktivieren?",
        stp_en_d: "Die Ports starten blockiert und brauchen bis zum doppelten Forward Delay, bis sie weiterleiten; Edge-Ports sind sofort wieder aktiv.",
        stp_dis_q: "Spanning Tree deaktivieren?",
        stp_dis_d: "Alle Ports leiten sofort weiter; der Schutz vor Netzwerkschleifen entfällt.",
        stp_cost_err: "Pfadkosten müssen zwischen 0 und 200000000 liegen",
        nav_lldp: "LLDP",
        lldp_enabled: "LLDP aktiv",
        lldp_title: "Dienststatus",
        lldp_portname: "Name",
        lldp_enable: "Pakete senden",
        stp_pcs: "Pfadkosten-Format",
        stp_pcs_long: "Lang (32 Bit)",
        stp_pcs_short: "Kurz (16 Bit)",
        stp_bh: "BPDUs bei ausgeschaltetem STP",
        stp_bh_flood: "Weiterleiten",
        stp_bh_filter: "Verwerfen",
        stp_r4: "Backup",
        stp_times: "Max. Alter / Forward Delay",
        stp_proto: "Protokoll",
        stp_nolink: "kein Link",
        stp_lasttc: "zuletzt",
        stp_mcheck: "Prüfen",
        stp_mcheck_t: "Erneut RST-BPDUs senden, um zu prüfen, ob der Nachbar noch 802.1D braucht",
        stp_cnt: "Zähler",
        stp_crx: "BPDUs empfangen",
        stp_ctx: "BPDUs gesendet",
        stp_ctrx: "TC empfangen",
        stp_cttx: "TC gesendet",
        stp_cage: "Letzte BPDU [s]",
        st_title: "Port-Statistik",
        st_h: "Summen seit dem Start",
        st_txg: "TX gut",
        st_txb: "TX fehlerhaft",
        st_rxg: "RX gut",
        st_rxb: "RX fehlerhaft",
        st_details: "Details",
        st_counters: "MIB-Zähler",
        st_nonzero: "nur ungleich null",
        st_autoref: "automatisch aktualisieren",
        st_counter: "Zähler",
        st_value: "Wert",
        st_fail: "Zähler konnten nicht geladen werden",
        v_title: "VLANs",
        v_vid: "VID",
        v_members: "Mitglieder",
        v_tagged: "Tagged",
        v_untagged: "Untagged",
        v_pvid_on: "PVID auf",
        v_empty: "Keine VLANs konfiguriert.",
        v_mgmt: "Management-VLAN",
        v_editor: "VLAN-Editor",
        v_optional: "optional",
        v_setmgmt: "Als Management-VLAN",
        v_setmgmt_t: "Dieses VLAN zum Management-VLAN machen",
        v_add: "Neues VLAN hinzufügen",
        v_member: "Mitglied",
        v_pvid: "PVID",
        v_legend: "U = Mitglied untagged, T = Mitglied tagged, - = kein Mitglied. PVID ordnet eingehenden Verkehr ohne Tag diesem VLAN zu.",
        v_accept: "Annehmen",
        v_ing_apply: "Ingress-Modi übernehmen",
        v_del_t: "VLAN löschen",
        v_rename: "Umbenennen",
        v_rename_t: "VLAN umbenennen",
        v_del_q: "VLAN {n} löschen?",
        v_del_d: "Die Ports behalten ihre PVID, bis sie neu zugewiesen wird.",
        v_vid_err: "VLAN-ID eingeben (1-4094)",
        v_loaded: "VLAN {n} geladen",
        v_notfound: "VLAN {n} nicht gefunden (es kann neu angelegt werden)",
        v_nomember: "Mindestens einen Mitglieds-Port wählen (oder das VLAN stattdessen löschen)",
        v_mgmt_tt: "Das Management-VLAN lässt sich nicht löschen. Lege zuerst ein anderes VLAN als Management-VLAN fest.",
        l2_title: "MAC-Adresstabelle",
        l2_filter: "filtern...",
        l2_flush: "Gelernte Einträge löschen",
        l2_static: "statisch",
        l2_learned: "gelernt",
        l2_loading: "lädt...",
        l2_failed: "Laden fehlgeschlagen",
        l2_entries: "Einträge",
        l2_del_t: "Eintrag löschen",
        l2_flush_q: "Alle gelernten MAC-Einträge löschen?",
        m_title: "Port-Spiegelung",
        m_active: "aktiv",
        m_monitor: "Monitor-Port",
        m_mirror: "Spiegeln",
        m_both: "Beide",
        m_note: "Beide = RX und TX des Ports auf den Monitor-Port spiegeln.",
        m_none: "Mindestens einen zu spiegelnden Port wählen",
        lag_hash: "Hash:",
        lag_clear_q: "LAG {n} auflösen?",
        lag_clear_d: "Alle Mitglieds-Ports kehren in den Normalbetrieb zurück.",
        lacp_heading: "LACP (802.3ad)",
        lag_mode: "Modus",
        lag_static: "Statisch",
        lacp_neg: "wird ausgehandelt",
        lacp_agg: "Aggregator",
        lacp_members: "aktive Mitglieder",
        lacp_actor: "Lokal",
        lacp_partner: "Partner",
        lacp_rxstate: "Empfangsstatus",
        lacp_rx: "Empfangene LACPDUs",
        lacp_psys: "Partner-System",
        bw_title: "Bandbreitenbegrenzung",
        bw_h: "Mbit/s, 0.016-10000",
        bw_in: "Eingangslimit",
        bw_out: "Ausgangslimit",
        bw_exceed: "Bei Überschreitung",
        bw_fc: "Flusskontrolle",
        bw_drop: "Verwerfen",
        bw_in_err: "Eingangslimit muss zwischen 0.016 und 10000 Mbit/s liegen",
        bw_out_err: "Ausgangslimit muss zwischen 0.016 und 10000 Mbit/s liegen",
        sc_title: "Storm Control",
        sc_h: "pro Port und Verkehrsart, leer = aus",
        sc_bcast: "Broadcast",
        sc_mcast: "Multicast",
        sc_ucast: "Unbekannter Unicast",
        sc_umcast: "Unbekannter Multicast",
        sc_err: "Grenzwert muss 1-1048575 pps oder 1-10000000 kbit/s betragen",
        sc_note: "Pakete einer Verkehrsart über ihrem Grenzwert werden beim Eintritt in den Switch verworfen.",
        sy_network: "Netzwerk",
        sy_mode: "Adressvergabe",
        sy_static: "Statisch",
        sy_dhcp_t: "Adresse per DHCP beziehen",
        sy_services: "Dienste",
        sy_igmp: "IGMP Snooping",
        sy_sysip: "Server-IP",
        sy_server: "Server",
        sy_port: "Port",
        sy_sesstmo: "Sitzungs-Timeout (s)",
        sy_sesstmo_err: "Sitzungs-Timeout muss zwischen 1 und 65535 s liegen",
        sy_services_note: "Der Dienststatus entspricht der Startkonfiguration; der laufende Zustand ist nicht auslesbar.",
        sy_password: "Admin-Passwort",
        sy_newpw: "Neues Passwort",
        sy_repeat: "Wiederholen",
        sy_pwapply: "Passwort ändern",
        sy_pw_note: "Wirkt sofort; im Flash speichern, damit es nach einem Neustart erhalten bleibt.",
        sy_console: "Konsole",
        sy_cmd: "CLI-Befehl...",
        sy_send: "Senden",
        sy_startup: "Startkonfiguration",
        sy_replayed: "wird bei jedem Start ausgeführt",
        sy_reload: "Aus dem Flash neu laden",
        sy_write: "In den Flash schreiben",
        sy_unknown_note: "Unbekannte Zeilen werden vor dem Schreiben hervorgehoben.",
        sy_maint: "Wartung",
        sy_reboot: "Switch neu starten",
        sy_lang: "Sprache",
        sy_ip_err: "Ungültige IP / Netzmaske / Gateway",
        sy_host_err: "Hostname: 1-23 druckbare Zeichen, keine Leerzeichen oder Anführungszeichen",
        sy_net_q: "Netzwerkeinstellungen übernehmen?",
        sy_ip_changed: "IP geändert, neu verbinden unter http://{ip}/",
        sy_dhcp_q: "Auf DHCP umstellen?",
        sy_sysip_err: "Ungültige IP des Syslog-Servers",
        sy_dns1: "DNS-Server",
        sy_dns2: "Zweiter DNS-Server",
        sy_dns_err: "Ungültige IP des DNS-Servers",
        sy_dns_dhcp: "von DHCP: {ip}",
        sy_sysport_err: "Syslog-Port muss zwischen 1 und 65535 liegen",
        sy_time: "Uhrzeit",
        sy_ntp: "NTP",
        sy_ntpsrv: "NTP-Server",
        sy_ntpsrv_p: "Hostname oder IPv4-Adresse",
        sy_ntpint: "Abfrageintervall [min]",
        sy_tz: "Zeitzone",
        sy_dst: "Sommerzeit",
        sy_dst_eu: "Europa",
        sy_dst_us: "USA",
        sy_now: "Lokale Zeit",
        sy_sync: "Letzte Synchronisation",
        sy_ntpaddr: "Server-Adresse",
        sy_nosync: "nicht synchronisiert",
        sy_waiting: "warte auf den Server",
        sy_ago: "vor {n} min",
        sy_ntp_err: "Hostname oder IPv4-Adresse des NTP-Servers eingeben",
        sy_ntpint_err: "Abfrageintervall muss zwischen 1 und 1440 liegen",
        sy_time_note: "Der Switch hat keine batteriegepufferte Uhr: Die Zeit geht bei jedem Neustart verloren und wird neu vom NTP-Server bezogen, sobald das Netzwerk bereit ist. Im Flash speichern, damit die Einstellungen erhalten bleiben.",
        sy_pw_len: "Passwort: 1-20 Zeichen",
        sy_pw_space: "Das Passwort darf keine Leerzeichen enthalten",
        sy_pw_match: "Die Passwörter stimmen nicht überein",
        sy_pw_q: "Admin-Passwort ändern?",
        sy_reboot_q: "Switch neu starten?",
        sy_reboot_d: "Es gibt UNGESPEICHERTE Änderungen, sie gehen verloren. Zuerst im Flash speichern, wenn du sie behalten willst.",
        sy_rebooting: "Neustart läuft, in etwa 20 s neu verbinden",
        sy_bytes: "Bytes",
        cw_title: "Diese Startkonfiguration schreiben?",
        cw_save_title: "Laufende Konfiguration im Flash speichern?",
        cw_info: "{n} / 2048 Bytes, wird bei jedem Start Zeile für Zeile ausgeführt",
        cw_toolarge: "Zu gross: Der Konfigurationssektor fasst höchstens 2048 Bytes. Zuerst Zeilen entfernen.",
        cw_longline: "Zeile {n} ist {m} Bytes lang: Der Switch kann beim Start nur Zeilen bis 126 Bytes ausführen und würde sie überspringen, deshalb wird das Schreiben abgelehnt. Zuerst kürzen.",
        cw_unknown: "Nicht in der bekannten Konfigurationssyntax (wird trotzdem geschrieben): ",
        cw_empty: "(leer)",
        cw_writing: "Konfiguration wird geschrieben...",
        cw_failed: "Schreiben der Konfiguration fehlgeschlagen: HTTP {n}",
        cw_verify_fail: "Prüfung fehlgeschlagen: Der Flash-Inhalt weicht vom Gesendeten ab. Befehlsprotokoll wurde NICHT gelöscht.",
        cw_saved: "Startkonfiguration gespeichert und geprüft",
        cw_collect: "Laufende Änderungen werden gesammelt...",
        t_applied: "Übernommen: {c}",
        t_cmds: "{n} Befehl(e) übernommen",
        t_rejected: "Befehl abgelehnt: {c}",
        t_failed: "fehlgeschlagen: {c}",
        fw_title: "Firmware-Update",
        fw_upload: "Hochladen",
        fw_checking: "{f}: {n} Bytes. Wird geprüft...",
        fw_size_err: "Grösse ist {n}, erwartet {w} oder 16384 (16-KiB-Loader)",
        fw_magic_err: "Bank-Header / LJMP-Kennung fehlt: kein Firmware-Image",
        fw_crc_err: "CRC16-Prüfung fehlgeschlagen: Image beschädigt",
        fw_valid: "gültiges RTLPlayground-Image (Grösse, Kennung und CRC16 in Ordnung)",
        fw_ldr_valid: "gültiges Loader-Image (Grösse, Kennung und CRC16 in Ordnung)",
        fw_md_valid: "gültige Maschinendaten (PoE-Firmware dieses Switches, CRC16 in Ordnung)",
        fw_md_d: "Sie werden an Ort und Stelle geschrieben und ab dem nächsten Start verwendet; sonst ändert sich am Switch nichts.",
        fw_md_done: "Maschinendaten geschrieben, ab dem nächsten Start verwendet ✓",
        fw_ldr_d: "Die laufende Firmware schreibt den Loader neu und startet den Switch danach neu. Nicht ausschalten, bis er zurück ist: ein beim Schreiben unterbrochener Loader braucht einen Flash-Programmer.",
        fw_ldr_failed: "kein Neustart erkannt: Das Schreiben des Loaders ist fehlgeschlagen. Den Switch nicht ausschalten, den Loader erneut hochladen oder die serielle Konsole prüfen.",
        fw_q: "Firmware hochladen?",
        fw_other: "Dieses Image wurde nicht für diesen Switch ({m}) gebaut: SFP-, LED- und Tasten-Pins können abweichen. Nur hochladen, wenn du sicher bist.",
        fw_d: "Ist die Prüfsumme korrekt, startet der Switch neu und übernimmt das Image beim Hochfahren (die Startkonfiguration bleibt erhalten). Nicht vom Strom trennen, bis er wieder erreichbar ist.",
        fw_finishing: "Flash wird fertig beschrieben... {s} s",
        fw_uploading: "wird hochgeladen... {p}% / {s} s",
        fw_verified: "Prüfsumme bestätigt, der Switch startet neu...",
        fw_rejected: "der Switch hat das Image abgelehnt (falsche Prüfsumme), es wurde nichts übernommen",
        fw_lost: "Hochladen fehlgeschlagen: Verbindung während der Übertragung abgebrochen",
        fw_noreboot: "kein Neustart erkannt: Das Image wurde sehr wahrscheinlich abgelehnt. Bei einem Update von alter Firmware nach dem erneuten Anmelden die Version in der Seitenleiste prüfen.",
        fw_applied: "Update übernommen",
        fw_done_t: "Firmware aktualisiert",
        fw_done: "Der Switch hat das Image geprüft und damit neu gestartet. Die Sitzung wurde zurückgesetzt, du wirst erneut zur Anmeldung aufgefordert.",
        fw_login: "Zur Anmeldung",
        fw_rebooting: "Switch startet neu...",
        fw_timeout: "Der Switch ist nach 150 s nicht zurück: Stromversorgung / serielle Konsole prüfen"
    }
};
var rtlLang = (function() {
    var s = null;
    try {
        s = localStorage.getItem("rtl_lang");
    } catch (e) {}
    if (s && LANG[s]) return s;
    var b = (navigator.language || "en").slice(0, 2);
    return LANG[b] ? b : "en";
})();

function t(k, v) {
    var s = LANG[rtlLang][k] || LANG.en[k] || k;
    if (v)
        for (var x in v) s = s.split("{" + x + "}").join(v[x]);
    return s;
}

function i18nApply() {
    document.querySelectorAll("[data-i18n]").forEach(function(el) {
        el.textContent = t(el.getAttribute("data-i18n"))
    });
    document.querySelectorAll("[data-i18n-t]").forEach(function(el) {
        el.title = t(el.getAttribute("data-i18n-t"))
    });
    document.querySelectorAll("[data-i18n-p]").forEach(function(el) {
        el.placeholder = t(el.getAttribute("data-i18n-p"))
    });
}

var S = {
    ports: [],
    n: 0,
    sfpSlot: [],
    pollN: 0,
    info: {},
    sys: {},
    loader: {},
    detail: null,
    tip: null,
    dirty: false,
    prev: null,
    prevT: 0,
    rates: [],
    mtu: [],
};
var $ = function(id) {
    return document.getElementById(id)
};

function h(tag, attrs, kids) {
    var e = document.createElement(tag);
    if (attrs)
        for (var k in attrs) {
            if (k === "text") e.textContent = attrs[k];
            else if (k === "html") e.innerHTML = attrs[k];
            else if (k.slice(0, 2) === "on") e.addEventListener(k.slice(2), attrs[k]);
            else e.setAttribute(k, attrs[k]);
        }
    if (kids) kids.forEach(function(c) {
        e.appendChild(c)
    });
    return e;
}

function esc(s) {
    return String(s).replace(/[&<>"]/g, function(c) {
        return {
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            '"': "&quot;"
        } [c]
    })
}

function badge(txt, cls) {
    return '<span class="badge' + (cls ? " " + cls : "") + '">' + esc(txt) + "</span>"
}

function linkBadge(p) {
    return !p.enabled ? badge(t("c_off")) : (p.link != "DOWN" ? badge(p.link, "ok") : badge(t("c_down")));
}

function applyTheme() {
    var pref;
    try {
        pref = localStorage.getItem("theme") || "auto";
    } catch (e) {
        pref = "auto";
    }
    var dark = window.matchMedia("(prefers-color-scheme: dark)").matches;
    var th = pref;
    if (pref === "auto") th = dark ? "dark" : "light";
    if (pref === "auto-sel") th = dark ? "sel-dark" : "sel-light";
    document.documentElement.dataset.theme = th;
    $("themeSel").value = pref;
}
$("themeSel").addEventListener("change", function() {
    try {
        localStorage.setItem("theme", this.value);
    } catch (e) {}
    applyTheme();
});
window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", applyTheme);
applyTheme();
$("langSel").value = rtlLang;
$("langSel").addEventListener("change", function() {
    try {
        localStorage.setItem("rtl_lang", this.value);
    } catch (e) {}
    location.reload();
});
i18nApply();

/* The firmware shares one output buffer across connections and delimits
 * responses by closing, so the next request must wait for the previous
 * BODY, not just its headers. */
var _q = Promise.resolve(),
    stgHold = 0;	/* apply's window: pollers pause; 1: requests wait 3 s */

function api(path, opts) {
    return new Promise(function(resolve, reject) {
        _q = _q.then(function() {
            opts = opts || {};
            var ctl = ("AbortController" in window) ? new AbortController() : null;
            if (ctl) opts.signal = ctl.signal;
            var to = setTimeout(function() {
                if (ctl) ctl.abort()
            }, stgHold === 1 ? 3000 : 10000);
            return fetch(path, opts).then(function(r) {
                if (r.status === 401) {
                    clearTimeout(to);
                    location.href = "/login.html";
                    throw new Error("auth");
                }
                return r.text().then(function(body) {
                    clearTimeout(to);
                    resolve({
                        ok: r.ok,
                        status: r.status,
                        body: body
                    });
                });
            }).catch(function(e) {
                clearTimeout(to);
                reject(e)
            });
        });
    });
}

function getJSON(p) {
    return api(p).then(function(r) {
        if (!r.ok) throw new Error(p + " " + r.status);
        return JSON.parse(r.body)
    })
}

/* what changes something is a POST (no side effects on a GET) */
function post() {
    return {
        method: "POST",
        body: ""
    }
}

function postJSON(p) {
    return api(p, post()).then(function(r) {
        if (!r.ok) throw new Error(p + " " + r.status);
        return JSON.parse(r.body)
    })
}

function getText(p) {
    return api(p).then(function(r) {
        if (!r.ok) throw new Error(p + " " + r.status);
        return r.body
    })
}

/* ---- The object model (DESIGN.md 14): CLI lines over POST /cli ---------- */

/* Lines in, their output out. Each command's output ends in one status
 * line, "ok" or "err <path>: <why>" (an old command's "Error: ..." counts
 * as one): r.out[i] is command i's lines, r.st[i] its status, r.errs the
 * failures' reasons. A reply httpd had to cut is a failure too. */
function cli(lines) {
    return api("/cli", {
        method: "POST",
        body: lines.join("\n")
    }).then(function(r) {
        if (!r.ok) throw new Error("/cli " + r.status);
        var res = {
                out: [],
                st: [],
                errs: []
            },
            cur = [];
        r.body.split("\n").forEach(function(l) {
            if (l === "[output truncated]") res.errs.push(l);
            else if (l === "ok" || /^err[ :]|^error\b/i.test(l)) {
                res.out.push(cur);
                res.st.push(l);
                if (l !== "ok") res.errs.push(l.replace(/^err:? /, ""));
                cur = [];
            } else if (l) cur.push(l);
        });
        if (cur.length) {           /* an old command that printed no status */
            res.out.push(cur);
            res.st.push("");
        }
        return res;
    });
}

/* paths into as few "get" lines as fit the cli's line: 128 B (CMD_BUF_SIZE)
 * and 15 words (N_WORDS), "get" and up to 14 paths */
function getLines(paths) {
    var out = [],
        l = "get",
        n = 0;
    paths.forEach(function(p) {
        if (l.length + p.length > 120 || n === 14) {
            out.push(l);
            l = "get";
            n = 0;
        }
        l += " " + p;
        n++;
    });
    out.push(l);
    return out;
}

/* "path value" lines into nested objects: "port.3.mtu 1500" -> o.port[3].mtu */
function parseLines(lines, o) {
    o = o || {};
    lines.forEach(function(l) {
        var sp = l.indexOf(" ");
        if (sp < 1) return;
        var k = l.slice(0, sp).split("."),
            n = o;
        for (var i = 0; i < k.length - 1; i++) n = n[k[i]] = n[k[i]] || {};
        n[k[i]] = l.slice(sp + 1);
    });
    return o;
}

/* "get path" as an object: stObj("state.ntp") -> {synced: "on", ...} */
function stObj(path) {
    return cli(["get " + path]).then(function(r) {
        var o = parseLines(r.out[0] || []);
        path.split(".").forEach(function(k) {
            o = (o || {})[k]
        });
        return o || {};
    });
}

/* A field of SCHEMA (schema.js, generated by om_gen.py --js and put in
 * front of this file) in the form the widgets read: t the type, e the
 * words, min/max, len, cpu, tip, u the unit, i its UI string (the schema's
 * label, or f_<object>_<field>) */
var OM_T = {
    b: "bool",
    e: "enum",
    n: "num",
    s: "str",
    m: "pmask"
};

function omField(obj, name) {
    var v = SCHEMA[obj].f[name],
        f = {
            t: OM_T[v[0]] || v[0],
            tip: v[5] || "",
            u: v[6],
            i: v[7] || ("f_" + obj + "_" + name).replace(/\./g, "_"),
            l: name
        };
    if (v[0] === "e") f.e = v[1];
    else if (v[0] === "n") {
        f.min = v[1];
        f.max = v[2];
    } else if (v[0] === "s") f.len = v[1];
    else if (v[0] === "m") f.cpu = v[1];
    return f;
}

/* Port masks as text ("1-4,9,cpu", "none", "all") and back: a set of port
 * numbers, "cpu" for the CPU port */
function pmParse(s, n) {
    var m = {};
    (s || "").split(",").forEach(function(it) {
        var r = it.split("-");
        if (it === "all") {
            for (var i = 1; i <= n; i++) m[i] = 1;
            m.cpu = 1;
        } else if (it === "cpu") m.cpu = 1;
        else if (+r[0])
            for (var j = +r[0]; j <= +(r[1] || r[0]); j++) m[j] = 1;
    });
    return m;
}

function pmText(m, n) {
    var out = [],
        a = 0;
    for (var i = 1; i <= n + 1; i++) {
        if (m[i] && i <= n) {
            if (!a) a = i;
        } else if (a) {
            out.push(a === i - 1 ? a : a + "-" + (i - 1));
            a = 0;
        }
    }
    if (m.cpu) out.push("cpu");
    return out.join(",") || "none";
}

/* A value the switch would refuse, checked here first: why, or "" */
function omCheck(f, v) {
    if (f.t === "num" && !(/^\d+$/.test(v) && +v >= f.min && +v <= f.max)) return f.min + "-" + f.max;
    if (f.t === "str" && (v.length > f.len || /["\\]|[^ -~]/.test(v))) return t("om_str", {
        n: f.len
    });
    return "";
}

/* Widgets bound to paths (W[path]): the candidate's value shown, marked
 * when it differs from running; an edit is staged at once ("set" into the
 * candidate). One being edited or focused keeps what the user has. */
var W = {};

function widget(path, f, nports) {
    var w = {
            f: f,
            path: path
        },
        el, inp;
    if (f.t === "bool") {
        inp = h("input", {
            type: "checkbox"
        });
        el = h("label", {
            class: "switch"
        }, [inp, h("i")]);
        w.get = function() {
            return inp.checked ? "on" : "off"
        };
        w.put = function(v) {
            inp.checked = v === "on"
        };
    } else if (f.t === "enum") {
        el = inp = h("select", {
            class: "in"
        });
        f.e.split(" ").forEach(function(o) {
            inp.appendChild(h("option", {
                value: o,
                text: o
            }));
        });
    } else if (f.t === "pmask") {
        var bs = {},
            m = {};
        el = h("span", {
            class: "seg pm"
        });
        for (var i = 1; i <= nports + 1; i++)(function(k) {
            bs[k] = h("button", {
                text: k === "cpu" ? "C" : k,
                title: k === "cpu" ? "CPU" : "",
                onclick: function() {
                    m[k] = !m[k];
                    bs[k].classList.toggle("on", !!m[k]);
                    stage(w);
                }
            });
            el.appendChild(bs[k]);
        })(i > nports ? "cpu" : i);
        w.get = function() {
            return pmText(m, nports)
        };
        w.put = function(v) {
            m = pmParse(v, nports);
            for (var k in bs) bs[k].classList.toggle("on", !!m[k]);
        };
    } else {
        el = inp = h("input", {
            class: "in" + (f.t === "num" ? " sm" : ""),
            type: f.t === "num" ? "number" : "text",
            size: "9"
        });
        if (f.t === "num") {
            inp.min = f.min;
            inp.max = f.max;
        } else inp.maxLength = f.len;
    }
    if (!w.get) {
        w.get = function() {
            return inp.value.trim()
        };
        w.put = function(v) {
            inp.value = v
        };
    }
    if (inp) {
        inp.addEventListener("input", function() {
            w.edit = 1
        });
        inp.addEventListener("change", function() {
            stage(w)
        });
    }
    el.title = f.tip || "";
    w.el = el;
    w.inp = inp;
    W[path] = w;
    if (stgHold) {		/* built in apply's window: locked as the rest */
        el.classList.add("lock");
        if (inp) inp.disabled = true;
    }
    return el;
}

/* The candidate's and running's values into the widgets; "" where a
 * field doesn't apply (its when rule) */
function wShow(path, cand, run) {
    var w = W[path];
    if (!w) return;
    var na = cand === undefined;
    w.el.classList.toggle("na", na);
    if (w.inp) w.inp.disabled = na;
    w.el.classList.toggle("stg", !na && cand !== run);
    if (na || w.edit || (w.inp && document.activeElement === w.inp)) return;
    w.put(cand);
    w.el.classList.remove("bad");
}

/* An edit into the candidate: "set path value", or "del" for an empty text */
function stage(w) {
    if (stgBusy()) {	/* locked in apply's window (the keys aren't) */
        w.edit = 0;
        return omReload();
    }
    var v = w.get(),
        why = omCheck(w.f, v);
    if (why) {
        w.el.classList.add("bad");
        toast(w.path + ": " + why, "err");
        return;
    }
    var pv = /^port\.(\d+)\.pvid$/.exec(w.path);	/* a LAG's members take one PVID (apply's check): all of them */
    (pv ? cli(["get -c lag"]) : Promise.resolve(null)).then(function(r) {
        var l = [w.path];
        if (r) {
            lagMembers(parseLines(r.out[0] || []).lag);
            for (var p in LGM[pv[1]])
                if (p !== pv[1]) l.push("port." + p + ".pvid");
            if (l.length > 1) toast(t("lag_pvid_moved", {
                p: pmText(LGM[pv[1]], S.n),
                v: v || "-"
            }), "ok");
        }
        return cli(l.map(function(x) {
            return v === "" ? "del " + x : "set " + x + " " + v
        }));
    }).then(function(r) {
        w.edit = 0;
        w.el.classList.toggle("bad", !!r.errs.length);
        if (r.errs.length) toast(r.errs[0], "err");
        omReload();
    }, function() {
        w.edit = 0
    });
}

/* ---- Staging: the candidate against running (DESIGN 5, 14.3) ---------- */

var omReloaders = [];

/* state.cfg into the bar: the pending count, Save while running differs
 * from saved */
function stgRefresh() {
    return cli(["get state.cfg"]).then(function(r) {
        var c = (parseLines(r.out[0] || []).state || {}).cfg || {};
        $("stgN").textContent = c.pending ? t("stg_n", {
            n: c.pending
        }) : "";
        $("stgSave").classList.toggle("show", c.dirty === "on");
    });
}

/* every tab bound to the model reloads its values, and the bar */
function omReload() {
    omReloaders.forEach(function(f) {
        f()
    });
    return stgRefresh().catch(function() {});
}

function stgShowDiff() {
    cli(["diff"]).then(function(r) {
        modal(t("stg_diff"), h("pre", {
            class: "mono small",
            text: r.out[0].join("\n") || t("stg_none")
        }), [h("button", {
            class: "ctl",
            text: t("c_cancel"),
            onclick: closeModal
        })]);
    });
}

function stgDiscard() {
    for (var k in W) W[k].edit = 0;
    cli(["discard"]).then(omReload);
}

/* Apply with a window: the change is kept only once the UI gets through
 * to the switch again and confirms, else the switch rolls it back */
var STG_WINDOW = 30,
    STG_LONG = 150;	/* confirmed from a new page: 2 min to log in there, and stgAsk asks above 30 s */

/* in apply's window: say so, and let the caller's reload show the
 * candidate (blurred: a focused field keeps what it has). Every edit
 * checks it: a second apply would end the window early */
function stgBusy() {
    if (stgHold) {
        toast(t("stg_busy"), "err");
        document.activeElement.blur();
    }
    return stgHold;
}

/* Apply, then follow the switch's state.cfg until it has
 * an outcome: confirm while it is open (the switch's countdown is the
 * deadline, not this browser's clock), wait while it walks */
function stgApply(win, go, tab) {
    if (stgBusy()) return Promise.resolve("busy");
    $("stgApply").disabled = true;
    stgHold = 2;		/* apply's reply may take long: 10 s */
    stgLock(1);
    return cli(["apply " + (win || STG_WINDOW)]).then(function(r) {
        if (r.errs.length) throw new Error(r.errs[0]);
        stgHold = 1;
        return go ? stgGo(go, tab) : stgWatch(60, 1);
    }, function() {
        /* its reply lost (the change may cut this browser off, or the
         * walk outlasts the reply): ask the switch */
        stgHold = 1;
        return go ? stgGo(go, tab) : stgWatch(60, 0);
    }).then(function(a) {
        if (a === "moved") toast(t("sy_ip_changed", {
            ip: go.split("/")[2]
        }), "ok");
        else if (a === "done") toast(t("stg_applied"), "ok");
        else toast(t(a === "rolled_back" ? "stg_rolled" : a === "failed" ? "stg_failed" :
            a === "unapplied" ? "stg_unapplied" : "stg_lost"), "err");
        return a;
    }, function(e) {
        toast(e.message, "err");
        return "refused";
    }).then(function(a) {
        stgHold = 0;
        stgLock(0);
        $("stgApply").disabled = false;
        omReload();
        return a;
    });
}

/* no edits while apply's window is open, so a change still pending after
 * it means apply never got there (.lock stops the pointer, not the keys:
 * stage() refuses those); unlocked, the reload shows every field again */
function stgLock(on) {
    for (var k in W) {
        if (!on) W[k].edit = 0;
        W[k].el.classList.toggle("lock", !!on);
        if (W[k].inp) W[k].inp.disabled = !!on || W[k].el.classList.contains("na");
    }
}

/* confirmed from elsewhere (the page there asks, stgAsk): go there; or,
 * the address moving, send the tab opened for it and wait here for the
 * window's end, about 130 s: no answer means the switch moved (moved),
 * else how it ended (a rollback) */
function stgGo(go, tab) {
    if (!tab) {
        /* another address and no tab (blocked): say where to come back */
        var u = location.protocol + "//" + location.host + "/";
        if (go.indexOf(u) && /^http/.test(go)) toast(t("sy_back", {
            u: u
        }), "ok");
        setTimeout(function() {
            location.href = go;
        }, /^http/.test(go) ? 4000 : 0);
        return new Promise(function() {});
    }
    tab.location = go;
    return stgWatch(STG_LONG / 5 + 2, 0).then(function(a) {	/* about 5 s a try */
        return a || "moved";
    });
}

/* a long window open when the UI starts (an address change or a new
 * password, which confirm from a new page; or another client's): reaching
 * the switch here is what confirm means, so ask */
function stgAsk() {
    return cli(["get state.cfg"]).then(function(r) {
        var c = (parseLines(r.out[0] || []).state || {}).cfg || {};
        if (c.apply === "confirm" && +c.confirm_s > STG_WINDOW) confirmModal(t("stg_keep_q"), t("stg_keep_d", {
            s: c.confirm_s
        }), function() {
            cli(["confirm"]).then(function(r) {
                toast(r.errs.length ? t("stg_rolled") : t("stg_applied"), r.errs.length ? "err" : "ok");
            }, function() {
                toast(t("stg_lost"), "err");
            }).then(omReload);
        });
    });
}

/* n tries 2 s apart: confirm while the window is open (at once if apply
 * said ok), wait while it walks or undoes; then its outcome: done,
 * rolled_back, failed, unapplied (apply never got there: the change is
 * still pending), or "" if the switch never answers */
function stgWatch(n, open) {
    return (open ? Promise.resolve("confirm") : cli(["get state.cfg"]).then(function(r) {
        var c = (parseLines(r.out[0] || []).state || {}).cfg || {},
            a = c.apply;
        if (r.errs.length || !a) throw 0;	/* err: applying (the walk) */
        $("stgN").textContent = t("stg_confirm", {
            s: c.confirm_s || 0
        });
        if (a === "running" || a === "undo") throw 0;
        if (a === "confirm" || a === "failed") return a;
        return c.pending > 0 ? "unapplied" : a;
    })).then(function(a) {
        if (a !== "confirm") return a;
        return cli(["confirm"]).then(function(r) {
            if (r.errs.length) throw 0;		/* the window ended: ask again */
            return "done";
        });
    }).catch(function() {
        if (!n) return "";
        return new Promise(function(ok) {
            setTimeout(ok, 2000)
        }).then(function() {
            return stgWatch(n - 1, 0)
        });
    });
}

function stgSave() {
    cli(["save"]).then(function(r) {
        toast(r.errs[0] || t("stg_saved"), r.errs.length ? "err" : "ok");
        omReload();
    });
}
$("stgDiffB").addEventListener("click", stgShowDiff);
$("stgDiscard").addEventListener("click", stgDiscard);
$("stgApply").addEventListener("click", function() {
    stgApply()
});
$("stgSave").addEventListener("click", stgSave);
stgRefresh().then(stgAsk).catch(function() {});

var CONF_CMDS = [
    /^ip\s+(\d{1,3}\.){3}\d{1,3}$/,
    /^ip\s+dhcp$/,
    /^gw\s+(\d{1,3}\.){3}\d{1,3}$/,
    /^netmask\s+(\d{1,3}\.){3}\d{1,3}$/,
    /^syslog\s+(on|off)$/,
    /^syslog\s+ip\s+(\d{1,3}\.){3}\d{1,3}$/,
    /^syslog\s+port\s+\d{1,5}$/,
    /^session\s+\d{1,5}$/,
    /^dns\s+server\s+(\d{1,3}\.){3}\d{1,3}(\s+(\d{1,3}\.){3}\d{1,3})?$/,
    /^ntp\s+(on|off)$/,
    /^ntp\s+server\s+[A-Za-z0-9.-]{1,47}$/,
    /^ntp\s+interval\s+\d{1,4}$/,
    /^ntp\s+timezone\s+[+-]?\d{1,2}(:\d{2})?$/,
    /^ntp\s+dst\s+(off|eu|us)$/,
    /^passwd\s+\S+$/, /^hostname\s+\S{1,23}$/,
    /^ingress(\s+\d+[tua])+$/, /^ingress\s+[tua]$/,
    /^port\s+\d+\s+(10m|100m|1g|2g5|5g|10g|auto|on|off)(\s+(half|full))?$/,
    /^port\s+\d+\s+name(\s+\S+)?$/,
    /^port\s+\d+\s+lldp\s+(permit|block)+$/,
    /^eee\s+(on|off)(\s+\d+)?(\s+(100m|1g|2g5))?$/,
    /^mirror(\s+\d+)(\s+\d+[tr]?)+$/, /^mirror\s+off$/,
    /^isolate\s+\d+(\s+(off|\d+))+$/,
    /^igmp\s+(on|off)$/,
    /^mtu\s+\d+\s+\d+$/,
    /^lldp\s+(on|off)$/,
    /^loop\s+(on|off)$/,
    /^led\s+(on|off)(\s+ports)?$/,
    /^bw\s+(in|out)\s+\d+\s+\S+$/,
    /^storm\s+\d+\s+(bcast|mcast|ucast|umcast)\s+(off|\d+\s+(pps|kbps))$/,
];

function isConfCmd(line) {
    for (var i = 0; i < CONF_CMDS.length; i++)
        if (CONF_CMDS[i].test(line)) return true;
    return false;
}

function setDirty(d) {
    S.dirty = d;
    $("dirty").classList.toggle("show", d);
    $("saveBtn").classList.toggle("pri", d);
}

function postCmd(cmd, quiet) {
    return api("/cmd", {
        method: "POST",
        body: cmd
    }).then(function(r) {
        if (!r.ok) throw new Error(((r.body || "").split("\n")[0]) || t("t_rejected", {
            c: cmd
        }));
        if (isConfCmd(cmd.trim())) setDirty(true);
        if (!quiet) toast(t("t_applied", {
            c: cmd
        }), "ok");
        return r;
    }, function(e) {
        toast(e.message || t("t_failed", {
            c: cmd
        }), "err");
        throw e;
    });
}

function postCmds(list) {
    var p = Promise.resolve();
    list.forEach(function(c) {
        p = p.then(function() {
            return postCmd(c, true)
        })
    });
    return p.then(function() {
        toast(t("t_cmds", {
            n: list.length
        }), "ok")
    });
}

function toast(msg, cls) {
    var el = h("div", {
        class: "toast " + (cls || ""),
        text: msg
    });
    $("toasts").appendChild(el);
    setTimeout(function() {
        el.style.opacity = "0";
        el.style.transition = "opacity .3s";
    }, 3400);
    setTimeout(function() {
        el.remove()
    }, 3800);
}

function modal(title, bodyEl, buttons) {
    S.detail = null;
    portTip(); // Hide if open
    $("mtitle").textContent = title;
    var b = $("mbody");
    b.innerHTML = "";
    b.appendChild(bodyEl);
    var f = $("mfoot");
    f.innerHTML = "";
    (buttons || []).forEach(function(bt) {
        f.appendChild(bt)
    });
    $("mback").classList.add("show");
}

function closeModal() {
    S.detail = null;
    $("mback").classList.remove("show")
}
$("mx").addEventListener("click", closeModal);
$("mback").addEventListener("click", function(e) {
    if (e.target === this) closeModal()
});

function confirmModal(title, detail, onok) {
    var b = h("div");
    if (detail) b.appendChild(h("p", {
        class: "small",
        text: detail
    }));
    modal(title, b, [
        h("button", {
            class: "ctl",
            text: t("c_cancel"),
            onclick: closeModal
        }),
        h("button", {
            class: "ctl pri",
            text: t("c_confirm"),
            onclick: function() {
                closeModal();
                onok()
            }
        }),
    ]);
}

var TABS = [{
        id: "dash",
        icon: "M3 13h4v8H3zM10 8h4v13h-4zM17 3h4v18h-4z"
    },
    {
        id: "ports",
        icon: "M2 7h20v10H2zM6 11v2M10 11v2M14 11v2M18 11v2"
    },
    {
        id: "stp",
        icon: "M12 3v5M12 8L5 13v8M12 8l7 5v8M2 21h20"
    },
    {
        id: "stats",
        icon: "M4 20V10M10 20V4M16 20v-7M22 20H2"
    },
    {
        id: "vlan",
        icon: "M12 3v6M12 9l-7 5M12 9l7 5M5 14v5M19 14v5M3 21h4M17 21h4"
    },
    {
        id: "l2",
        icon: "M4 5h16M4 12h16M4 19h10"
    },
    {
        id: "mirror",
        icon: "M12 3v18M7 8l-4 4 4 4M17 8l4 4-4 4"
    },
    {
        id: "lag",
        icon: "M7 8a4 4 0 100 8h3M17 8a4 4 0 110 8h-3M9 12h6"
    },
    {
        id: "lldp",
        icon: "M11 12a1 1 0 002 0 1 1 0 00-2 0M8.5 8.5a5 5 0 000 7M15.5 8.5a5 5 0 010 7M5.6 5.6a9 9 0 000 12.8M18.4 5.6a9 9 0 010 12.8"
    },
    {
        id: "bw",
        icon: "M4 18a8 8 0 0116 0M12 18l4-6"
    },
    {
        id: "system",
        icon: "M12 8a4 4 0 100 8 4 4 0 000-8zM4 12h2M18 12h2M12 4v2M12 18v2M6 6l1.5 1.5M16.5 16.5L18 18M18 6l-1.5 1.5M7.5 16.5L6 18"
    },
    {
        id: "fw",
        icon: "M12 3v12M8 11l4 4 4-4M4 19h16"
    },
    {
        id: "schema",
        icon: "M4 4h16v16H4zM4 10h16M10 10v10"
    },
];
var curTab = "dash";
var tabHooks = {};

function showTab(id) {
    var old = tabHooks[curTab];
    if (old && old.leave) old.leave();
    curTab = id;
    TABS.forEach(function(tb) {
        $("tab-" + tb.id).classList.toggle("act", tb.id === id);
        $("nv-" + tb.id).classList.toggle("act", tb.id === id);
    });
    $("ttitle").textContent = t("nav_" + id);
    $("nav").classList.remove("open");
    if (location.hash !== "#" + id) history.replaceState(null, "", "#" + id);
    var hk = tabHooks[id];
    if (hk && hk.enter) hk.enter();
}
TABS.forEach(function(tb) {
    $("navlist").appendChild(h("li", {
        id: "nv-" + tb.id,
        onclick: function() {
            showTab(tb.id)
        }
    }, [
        h("span", {
            html: '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke-width="2"><path d="' + tb.icon + '"/></svg>'
        }),
        h("span", {
            text: t("nav_" + tb.id)
        }),
    ]));
});
$("burger").addEventListener("click", function() {
    $("nav").classList.toggle("open")
});

var IDLE_STOP_MS = 600000,
    lastInput = Date.now();
["mousemove", "mousedown", "keydown", "touchstart", "wheel"].forEach(function(ev) {
    addEventListener(ev, function() {
        lastInput = Date.now()
    }, {
        passive: true
    });
});

function Poller(fn, ms) {
    this.fn = fn;
    this.ms = ms;
    this.on = false;
    this.t = null
}
Poller.prototype.start = function() {
    if (this.on) return;
    this.on = true;
    this.tick()
};
Poller.prototype.stop = function() {
    this.on = false;
    clearTimeout(this.t)
};
Poller.prototype.tick = function() {
    var self = this;
    if (!self.on) return;
    var quiet = stgHold || document.hidden || Date.now() - lastInput > IDLE_STOP_MS;
    var run = quiet ? Promise.resolve() : Promise.resolve().then(self.fn).catch(function() {});
    run.then(function() {
        if (self.on) self.t = setTimeout(function() {
            self.tick()
        }, self.ms);
    });
};

function byPort(a) {
    return a.sort(function(x, y) {
        return x.port - y.port
    })
}

/* The port strip's poll (DESIGN 14.4) on state.port, and the config it
 * shows (portsCfg: at start and after a staging reload); S.ports keeps the
 * old shape: link "DOWN" or the speed's text, txG..rxB decimal. A web
 * reply costs its bytes (the console mirrors it, ~0.09 ms a byte), so the
 * poll asks only what the open view shows: link and speed always, the good
 * counters on the dashboard and stats, the bad ones there too (every 4th
 * poll on the dashboard); a field not asked keeps its last value */
var LSPEED = {
    "10m": "10M",
    "100m": "100M",
    "500m": "500M",
    "1g": "1000M",
    "2g5": "2.5G",
    "5g": "5G",
    "10g": "10G"
};

function gets(paths) {	/* get paths (state.port.link: every port's), as few lines as fit, into one object */
    return cli(getLines(paths)).then(function(r) {
        var o = {};
        r.out.forEach(function(l) {
            parseLines(l, o)
        });
        return o;
    });
}

function portsCfg() {
    return gets(["port.name", "port.enabled", "port.speed", "state.port.media"]).then(function(o) {
        S.pcfg = o;
    });
}
omReloaders.push(function() {
    portsCfg().catch(function() {})
});

function pollStatus() {
    return (S.pcfg ? Promise.resolve() : portsCfg()).then(function() {
        var f = ["state.port.link", "state.port.speed", "state.sfp.present"],
            ct = curTab === "dash" || curTab === "stats";
        if (ct) f.push("state.port.tx_good", "state.port.rx_good");
        if (curTab === "stats" || ct && !(S.pollN++ & 3)) f.push("state.port.tx_bad", "state.port.rx_bad");
        return gets(f);
    }).then(function(o) {
        var c = S.pcfg.port || {},
            m = (S.pcfg.state || {}).port || {},
            st = (o.state || {}).port || {},
            sfp = (o.state || {}).sfp || {},
            s = [],
            now = Date.now();
        Object.keys(c).sort(function(a, b) {
            return a - b
        }).forEach(function(n) {
            var x = st[n] || {},
                q = (S.ports || [])[n - 1] || {},
                sp = m[n] && m[n].media === "sfp";
            s.push({
                port: +n,
                name: c[n].name,
                isSFP: sp,
                cspeed: c[n].speed,
                link: x.link !== "up" ? "DOWN" : LSPEED[x.speed] || "?",
                txG: x.tx_good || q.txG || "0",
                rxG: x.rx_good || q.rxG || "0",
                txB: x.tx_bad || q.txB || "0",
                rxB: x.rx_bad || q.rxB || "0"
            });
        });
        var first = !S.n;
        S.ports = s;
        if (first) {
            S.n = s.length;
            var slot = 0;
            s.forEach(function(p) {
                if (p.isSFP) {
                    slot++;
                    S.sfpSlot[p.port - 1] = slot;
                }
            });
        }
        s.forEach(function(p) {	/* an SFP port is on with a module in its cage */
            p.enabled = p.isSFP ? (sfp[S.sfpSlot[p.port - 1]] || {}).present === "on" : c[p.port].enabled === "on";
        });
        if (first) buildStrip();
        if (!(o.state && o.state.port && "1" in o.state.port && "tx_good" in o.state.port["1"])) return updateStrip(), hkStatus();
        if (S.prev) {
            var dt = (now - S.prevT) / 1000;
            if (dt > 0.2) s.forEach(function(p, i) {
                var q = S.prev[i];
                if (q) S.rates[p.port - 1] = {
                    tx: Number(BigInt(p.txG) - BigInt(q.txG)) / dt,
                    rx: Number(BigInt(p.rxG) - BigInt(q.rxG)) / dt,
                };
            });
        }
        S.prev = s.map(function(p) {	/* a copy: the open detail writes newer counters into S.ports */
            return {
                txG: p.txG,
                rxG: p.rxG
            };
        });
        S.prevT = now;
        updateStrip();
        hkStatus();
    });
}

function hkStatus() {
    var hk = tabHooks[curTab];
    if (hk && hk.status) hk.status();
}
var statusPoller = new Poller(pollStatus, 2500);
statusPoller.start();

function needPorts(fn) {
    if (S.n) return fn();
    pollStatus().then(fn).catch(function() {});
}

function fmtPps(v) {
    if (v == null) return "-";
    if (v >= 1e6) return (v / 1e6).toFixed(1) + " M";
    if (v >= 1e3) return (v / 1e3).toFixed(1) + " k";
    return Math.round(v);
}

function portLabel(p) {
    return p.name ? p.port + " " + p.name : String(p.port)
}

function portTip(i) {
    S.tip = (typeof i === "number" && S.detail == null && S.ports[i]) ? i : null;
    var tip = $("ptip");
    tip.classList.toggle("show", S.tip != null);
    if (S.tip == null) return;
    tip.innerHTML = "";
    tip.appendChild(detailTable(S.ports[S.tip]));
    var r = $("pp" + S.tip).getBoundingClientRect();
    var left = Math.max(8, Math.min(innerWidth - tip.offsetWidth - 8, r.left + (r.width - tip.offsetWidth) / 2));
    tip.style.top = (r.bottom + 8) + "px";
    tip.style.left = left + "px";
    tip.style.setProperty("--ax", (r.left + r.width / 2 - left) + "px");
}
["scroll", "resize"].forEach(function(ev) {
    addEventListener(ev, portTip, {
        passive: true,
        capture: true
    })
});

function buildStrip() {
    var st = $("strip");
    st.innerHTML = "";
    for (var i = 0; i < S.n; i++)(function(i) {
        var p = S.ports[i] || {};
        st.appendChild(h("div", {
            class: "port" + (p.isSFP ? " sfp" : ""),
            id: "pp" + i,
            onclick: function() {
                portDetail(i)
            },
            onpointerenter: function(e) {
                // Don't momentarily flash the modal when tapping via touchscreen
                if (e.pointerType !== "touch") portTip(i)
            },
            onpointerleave: portTip
        }, [
            h("span", {
                class: "pn",
                text: String(i + 1)
            }),
            h("i", {
                class: "jack"
            }),
            h("span", {
                class: "ps",
                id: "ppl" + i,
                text: "..."
            }),
        ]));
    })(i);
}

function updateStrip() {
    S.ports.forEach(function(p) {
        var i = p.port - 1,
            el = $("pp" + i),
            lb = $("ppl" + i);
        if (!el) return;
        el.classList.toggle("dis", !p.enabled);
        var up = p.enabled && p.link != "DOWN";
        el.classList.toggle("up", !!up);
        if (up) el.style.setProperty("--pc", "var(" + "--s" + p.link.replace(/\-[HF]D$/,"").replace("2.5G","2G5").toLowerCase() + ")");
        lb.textContent = !p.enabled ? t("c_off") : (p.link != "DOWN" ? p.link : t("c_down"));
    });
    if (S.tip != null) portTip(S.tip);
    if (S.detail != null) detailShow(S.detail);
}

function pU16(v) {
    return parseInt(v, 16) & 0xffff
}

function pI16(v) {
    var x = parseInt(v, 16),
        n = x & 0x7fff;
    return (x & 0x8000) ? n - 0x8000 : n
}

function calSO(val, cal) {
    if (typeof cal !== "string") return val;
    if (cal.slice(0, 2) === "0x") cal = cal.slice(2);
    if (cal.length !== 8) return val;
    return (pU16(cal.slice(0, 4)) / 256) * val + pI16(cal.slice(4, 8));
}

function calRx(val, cal) {
    if (typeof cal !== "string") return val;
    if (cal.slice(0, 2) === "0x") cal = cal.slice(2);
    if (cal.length !== 40) return val;
    var b = cal.match(/.{2}/g).map(function(x) {
        return parseInt(x, 16)
    });
    var v = new DataView(new Uint8Array(b).buffer);
    return v.getFloat32(0) * Math.pow(val, 4) + v.getFloat32(4) * Math.pow(val, 3) +
        v.getFloat32(8) * Math.pow(val, 2) + v.getFloat32(12) * val + v.getFloat32(16);
}

/* "0.603 mW / -2.20 dBm"; a dark port (under 1e-4 mW, the word's 0.1 uW) "- dBm" */
function mwText(mw) {
    return mw.toFixed(3) + " mW / " + (mw < 1e-4 ? "-" : (10 * Math.log10(mw)).toFixed(2)) + " dBm"
}

/* state.sfp.S's diagnostics in units: the raw SFF-8472 words back (vcc_v
 * and the powers in 1/10000, temp an int16 in 1/256 C, tx_bias in 2 uA),
 * calibrated with ddm.cal (bytes 184-223: the RX polynomial at hex 0-39,
 * then the slope/offset pairs of tx_bias, tx_power, temp, vcc) when the
 * module carries it */
function sfpDdm(o) {
    var d = o.ddm || {},
        cal = d.cal || "",
        tc = +d.temp;

    function c(i) {
        return cal.length === 80 ? cal.slice(40 + 8 * i, 48 + 8 * i) : null
    }
    return {
        temp: calSO(tc > 32767 ? tc - 65536 : tc, c(2)) / 256,
        vcc: calSO(Math.round(d.vcc_v * 10000), c(3)) / 10000,
        bias: calSO(+d.tx_bias, c(0)) / 500,
        tx: calSO(Math.round(d.tx_power_mw * 10000), c(1)) / 10000,
        rx: calRx(Math.round(d.rx_power_mw * 10000), cal.slice(0, 40)) / 10000
    };
}

function portRows(p) {
    var rows = [
        [t("c_port"), String(p.port)],
        [t("c_type"), p.isSFP ? "SFP" : "RJ45"]
    ];
    if (p.name) rows.push([t("c_name"), p.name]);
    rows.push([t("p_state"), !p.enabled ? t("p_disabled") : (p.link != "DOWN" ? t("p_up") + " " + p.link +
        (p.duplex === "half" ? " " + t("c_half") : "") : t("c_down"))]);
    rows.push([t("st_txg"), BigInt(p.txG) + " " + t("p_pkts")]);
    rows.push([t("st_txb"), BigInt(p.txB) + " " + t("p_pkts")]);
    rows.push([t("st_rxg"), BigInt(p.rxG) + " " + t("p_pkts")]);
    rows.push([t("st_rxb"), BigInt(p.rxB) + " " + t("p_pkts")]);
    if (p.isSFP) {
        /* state.sfp.S (detailShow) */
        var o = p.sfp || {},
            st = ((o.ddm || {}).status || "").split(","),
            ext = o.ddm_ok === "on";
        if (o.present === "on") rows.push([t("p_module"), [o.vendor, o.part, o.serial].filter(Boolean).join(" / ")]);
        if (ext) {
            var dd = sfpDdm(o);
            rows.push([t("p_temp"), dd.temp.toFixed(1) + " \u00b0C"]);
            rows.push([t("p_vcc"), dd.vcc.toFixed(2) + " V"]);
            rows.push([t("p_txbias"), dd.bias.toFixed(1) + " mA"]);
            rows.push([t("p_txpower"), mwText(dd.tx)]);
            rows.push([t("p_rxpower"), mwText(dd.rx)]);
            rows.push([t("p_txfault"), t(st.indexOf("tx_fault") >= 0 ? "c_yes" : "c_no")]);
            rows.push([t("p_txdis"), t(st.indexOf("tx_disable") >= 0 ? "c_yes" : "c_no")]);
        }
        var losPin = o.los === "ok" ? false : o.los === "los" ? true : null;
        var losMod = ext ? st.indexOf("rx_los") >= 0 : null;
        if (losPin != null || losMod != null) {
            var v = (losMod != null && losPin != null && losMod !== losPin) ?
                ("pin=" + losPin + " mod=" + losMod + " !") : t((losMod != null ? losMod : losPin) ? "c_yes" : "c_no");
            rows.push([t("p_rxlos"), v]);
        }
    } else if (p.cspeed) {
        rows.push([t("pt_speed"), p.cspeed === "auto" ? t("c_auto") : p.cspeed]);	/* the config's, not the PHY's */
    }
    return rows;
}

function detailTable(p) {
    var tb = h("table", { class: "t" });
    portRows(p).forEach(function(r) {
        tb.appendChild(h("tr", null, [h("td", { class: "mut", text: r[0] }), h("td", { text: r[1] })]));
    });
    return tb;
}

/* the port detail's table; an SFP port's from its cage's state.sfp.S */
function detailShow(i, first) {
    var p = S.ports[i],
        n = "state.port." + (i + 1) + ".";
    gets([n + "tx_good", n + "rx_good", n + "tx_bad", n + "rx_bad"].concat(p.isSFP ? [] : [n + "duplex"])).then(function(o) {	/* duplex: copper only */
        var x = o.state.port[i + 1];
        p.txG = x.tx_good, p.rxG = x.rx_good, p.txB = x.tx_bad, p.rxB = x.rx_bad;
        p.duplex = x.duplex;
        return p.isSFP ? stObj("state.sfp." + S.sfpSlot[i]) : null;
    }).then(function(o) {
        p.sfp = o;
        if (first) return modal(t("c_port") + " " + (i + 1), detailTable(p));
        var b = $("mbody");
        b.innerHTML = "";
        b.appendChild(detailTable(p));
    }).catch(function() {});
}

function portDetail(i) {
    if (!S.ports[i]) return;
    detailShow(i, 1);
    S.detail = i;
}

/* seconds -> "1 d 00:00:00" in the page language */
function fmtUptime(s) {

    function p(n) {
        return String(n).padStart(2, "0");
    }
    var t3 = p(Math.floor(s / 3600) % 24) + ":" + p(Math.floor(s / 60) % 60) + ":" + p(s % 60),
        d = Math.floor(s / 86400);
    return d ? t("i_uptime_d", {
        d: d,
        t: t3
    }) : t3;
}

/* The dashboard's card: the build, board and chip, from state.sys (P5-1).
 * The hostname and the addresses come back with their objects (P4-1, P5-3). */
function renderInfo() {
    var y = S.sys,
        m = [
            ["i_mac", y.mac],
            ["i_fw", y.version],
            ["i_built", y.build_date],
            ["i_uptime", y.uptime_s && fmtUptime(+y.uptime_s)],
            ["i_hw", y.machine],
            ["i_pcb", y.pcb],
            ["i_temp", y.temp_c && y.temp_c + " \u00b0C"],
            ["i_flash", y.flash && (y.flash.size >= 1048576 ? y.flash.size / 1048576 + " MiB" : y.flash.size / 1024 + " KiB")],
            ["i_reset", y.reset && y.reset.cause]
        ];
    var tb = $("sysinfo");
    tb.innerHTML = "";
    m.forEach(function(r) {
        if (r[1] == null || r[1] === "") return;
        tb.appendChild(h("tr", null, [h("td", {
            class: "mut",
            text: t(r[0])
        }), h("td", {
            class: "mono",
            text: String(r[1])
        })]));
    });
    if (y.version) $("fver").textContent = "RTLPlayground " + y.version;
}

/* state.sys and state.sys.reset into S.sys, state.loader into S.loader */
function stSysLoad() {
    return cli(["get state.sys", "get state.sys.reset", "get state.loader"]).then(function(r) {
        var o = {};
        r.out.forEach(function(ls) {
            parseLines(ls, o)
        });
        o = o.state || {};
        S.sys = o.sys || {};
        S.loader = o.loader || {};
    });
}

/* the switch's name in the header: sys.hostname (the derived one when empty) */
function hostLoad() {
    return stObj("sys").then(function(o) {
        S.host = o.hostname || "";
        if (S.host) {
            $("brandname").textContent = $("brandname").title = document.title = S.host;
            try {
                localStorage.setItem("rtl_host", S.host);
            } catch (e) {}
        }
    });
}

function dashStatus() {
    var tb = $("traffic").tBodies[0];
    if (tb.rows.length !== S.n) {
        tb.innerHTML = "";
        for (var i = 0; i < S.n; i++) {
            var tr = tb.insertRow();
            for (var c = 0; c < 6; c++) tr.insertCell().className = c >= 2 ? "num" : "";
        }
    }
    S.ports.forEach(function(p) {
        var r = tb.rows[p.port - 1];
        if (!r) return;
        var rt = S.rates[p.port - 1];
        r.cells[0].textContent = portLabel(p);
        r.cells[1].innerHTML = linkBadge(p);
        r.cells[2].textContent = rt ? fmtPps(rt.tx) : "-";
        r.cells[3].textContent = rt ? fmtPps(rt.rx) : "-";
        r.cells[4].textContent = BigInt(p.txB).toString();
        r.cells[5].textContent = BigInt(p.rxB).toString();
    });
}
tabHooks.dash = {
    enter: function() {
        stSysLoad().then(renderInfo).catch(function() {});
    },
    status: dashStatus,
};

/* ---- Tables bound to paths: rows the ports, columns an object's fields -- */

function label(f) {
    var s = t(f.i);
    return (s === f.i ? f.l : s) + (f.u ? " (" + f.u + ")" : "")
}

function tHead(tbl, obj, cols) {
    var hr = tbl.createTHead().insertRow();
    hr.appendChild(h("th", {
        text: t("c_port")
    }));
    cols.forEach(function(c) {
        var f = omField(obj, c);
        hr.appendChild(h("th", {
            text: label(f),
            title: f.tip
        }));
    });
}

/* config: a widget on obj.N.field in each cell */
function cfgTable(tbl, obj, cols, ports) {
    tbl.innerHTML = "";
    tHead(tbl, obj, cols);
    var tb = tbl.createTBody();
    ports.forEach(function(n) {
        var tr = tb.insertRow();
        tr.insertCell().textContent = n;
        cols.forEach(function(c) {
            tr.insertCell().appendChild(widget(obj + "." + n + "." + c, omField(obj, c), ports.length));
        });
    });
}

/* state, read only: the cells filled by stShow() */
function stTable(tbl, obj, cols, ports) {
    tbl.innerHTML = "";
    tHead(tbl, obj, cols);
    var tb = tbl.createTBody();
    ports.forEach(function(n) {
        var tr = tb.insertRow();
        tr.insertCell().textContent = n;
        cols.forEach(function(c) {
            tr.insertCell().id = obj + "." + n + "." + c;
        });
    });
}

function stShow(o, obj, cols, ports) {
    ports.forEach(function(n) {
        cols.forEach(function(c) {
            var el = $(obj + "." + n + "." + c);
            if (el) el.textContent = (o[n] || {})[c] || "-";
        });
    });
}

/* ---- The port tab, on port.N and state.port.N ------------------------- */

var PT_CFG = ["name", "enabled", "speed", "duplex", "mtu", "pvid", "ingress", "ingress_filter", "lldp", "eee", "isolate"],
    PT_ST = ["link", "speed", "duplex", "media", "ms_role", "macs", "eee_active"],
    ptPorts = [];

/* running and candidate: the widgets (built once the ports are known) */
function ptLoad() {
    return cli(["get port", "get -c port"]).then(function(r) {
        var run = parseLines(r.out[0]).port || {},
            cand = parseLines(r.out[1]).port || {},
            ports = Object.keys(cand).map(Number).sort(function(a, b) {
                return a - b
            });
        if (ports.join() !== ptPorts.join()) {
            ptPorts = ports;
            cfgTable($("ptable"), "port", PT_CFG, ports);
            stTable($("pstate"), "state.port", PT_ST, ports);
        }
        ports.forEach(function(n) {
            PT_CFG.forEach(function(c) {
                wShow("port." + n + "." + c, cand[n][c], (run[n] || {})[c]);
            });
        });
    });
}

function ptState() {
    return cli(getLines(PT_ST.map(function(c) {
        return "state.port.*." + c
    }))).then(function(r) {
        var o = {};
        r.out.forEach(function(ls) {
            parseLines(ls, o)
        });
        stShow((o.state || {}).port || {}, "state.port", PT_ST, ptPorts);
    });
}
var ptPoller = new Poller(ptState, 3000);
omReloaders.push(function() {
    if (curTab === "ports") ptLoad().catch(function() {});
});
tabHooks.ports = {
    enter: function() {
        ptLoad().then(function() {
            ptPoller.start()
        }).catch(function() {});
    },
    leave: function() {
        ptPoller.stop()
    },
};

/* /stp.json's runtime flags (its config is stp.*, P4-4) */
var STP_PF = {
    OPEREDGE: 64,
    TRIP: 128
};
var stpSig = "",
    stpCur = null,
    STPC = {},	/* get -c stp: the candidate's stp, stp.port.N, stp.lag.L */
    STPB = {	/* the bridge card's elements: their stp field */
        stpver: "version",
        stppcs: "pathcost",
        stpbh: "bpdu",
        stpprio: "priority",
        stphello: "hello_s",
        stpmaxage: "maxage_s",
        stpfwd: "fwd_delay_s",
        stptxhold: "txhold"
    },
    STPF = {	/* a port row's (id + key) */
        sten: "enabled",
        sted: "edge",
        stco: "cost",
        stpr: "priority",
        stgu: "guard",
        stfi: "filter",
        stpp: "p2p"
    };

function stpSel(id, opts) {
    var s = h("select", {
        class: "in",
        id: id,
        onchange: stpTouch
    });
    opts.forEach(function(o) {
        s.appendChild(h("option", {
            value: o[0],
            text: o[1]
        }))
    });
    return s;
}

function stpTouch() {
    this.closest("tr,.card").dataset.dirty = "1"
}

function fmtBridge(hex) {
    if (!hex || hex.length < 16) return "-";
    return parseInt(hex.slice(0, 4), 16) + " / " + hex.slice(4).replace(/(..)(?=.)/g, "$1:");
}

function stpEnt(pt) {
    pt.lag = pt.p > 100 ? pt.p - 100 : 0
}

function stpKey(pt) {
    return pt.lag ? "L" + pt.lag : String(pt.p)
}

function stpName(pt) {
    return pt.lag ? "LAG" + pt.lag + " (" + rangeStr(pt.mbr) + ")" : String(pt.p)
}

function stpDhms(x) {
    return Math.floor(x / 86400) + "d " + Math.floor(x / 3600) % 24 + "h " + Math.floor(x / 60) % 60 + "m " + x % 60 + "s"
}

function stpPath(k) {
    return k.charAt(0) === "L" ? "stp.lag." + k.slice(1) : "stp.port." + k
}

/* entity k's fields in o (get stp's parsed) */
function stpObj(k, o) {
    return (k.charAt(0) === "L" ? (o.lag || {})[k.slice(1)] : (o.port || {})[k]) || {}
}

/* elements f (id + sfx: field) into the candidate's c, or c's values into them */
function stpVal(e) {
    return e.type === "checkbox" ? (e.checked ? "on" : "off") : e.value.trim()
}

function stpPut(f, sfx, c) {
    for (var id in f) {
        var e = $(id + sfx);
        if (e.type === "checkbox") e.checked = c[f[id]] === "on";
        else e.value = c[f[id]];
    }
}

/* the fields that differ from c as set lines into path, then the bar's
 * Apply; a refused set puts c back */
function stpStage(f, sfx, path, c) {
    var l = [],
        u = [];
    for (var id in f) {
        var v = stpVal($(id + sfx));
        if (v === c[f[id]]) continue;
        l.push("set " + path + "." + f[id] + " " + v);
        u.push("set " + path + "." + f[id] + " " + c[f[id]]);
    }
    if (l.length) omSet(l, u).then(stpLoad);
}

/* do path.act, at once (no apply) */
function stpDo(k, act) {
    cli(["do " + stpPath(k) + "." + act]).then(function(r) {
        if (r.errs.length) toast(r.errs[0], "err");
    }).catch(function() {}).then(stpLoad);
}

function stpBuild(ports) {
    var tb = $("stpcfg").tBodies[0],
        st = $("stpstat").tBodies[0],
        ct = $("stpcnt").tBodies[0];
    tb.innerHTML = "";
    st.innerHTML = "";
    ct.innerHTML = "";
    ports.forEach(function(pt) {
        var p = stpKey(pt);
        var tr = tb.insertRow();
        tr.insertCell().textContent = stpName(pt);
        var sw = h("label", {
            class: "switch"
        }, [h("input", {
            type: "checkbox",
            id: "sten" + p,
            onchange: stpTouch
        }), h("i")]);
        tr.insertCell().appendChild(sw);
        tr.insertCell().appendChild(stpSel("sted" + p, [
            ["auto", t("c_auto")],
            ["on", t("c_on")],
            ["off", t("c_offc")]
        ]));
        tr.insertCell().appendChild(h("input", {
            class: "in",
            id: "stco" + p,
            type: "number",
            min: "0",
            max: "200000000",
            style: "width:9em",
            onchange: stpTouch
        }));
        var pr = [];
        for (var v = 0; v <= 240; v += 16) pr.push([String(v), String(v)]);
        tr.insertCell().appendChild(stpSel("stpr" + p, pr));
        tr.insertCell().appendChild(stpSel("stgu" + p, [
            ["none", t("stp_g_none")],
            ["bpdu", t("stp_g_bpdu")],
            ["root", t("stp_g_root")]
        ]));
        tr.insertCell().appendChild(stpSel("stfi" + p, [
            ["off", t("c_offc")],
            ["on", t("c_on")]
        ]));
        tr.insertCell().appendChild(stpSel("stpp" + p, [
            ["auto", t("c_auto")],
            ["on", t("c_on")],
            ["off", t("c_offc")]
        ]));
        tr.insertCell().appendChild(h("button", {
            class: "ctl",
            text: t("c_apply"),
            onclick: function() {
                stpApplyPort(p)
            }
        }));
        var sr = st.insertRow();
        sr.insertCell().textContent = stpName(pt);
        ["ro", "st", "db", "dp", "dc", "oe", "op", "pv"].forEach(function(k) {
            sr.insertCell().id = "st" + k + p
        });
        sr.insertCell().appendChild(h("button", {
            class: "ctl",
            id: "stmc" + p,
            text: t("stp_mcheck"),
            title: t("stp_mcheck_t"),
            onclick: function() {
                stpDo(p, "mcheck")
            }
        }));
        var cr = ct.insertRow();
        cr.insertCell().textContent = stpName(pt);
        ["rx", "tx", "trx", "ttx", "age"].forEach(function(k) {
            cr.insertCell().id = "sc" + k + p
        });
    });
}

/* the counters panel: state.stp.port.N and state.stp.lag.L (P5-4) */
function stpCnt() {
    return cli(["get state.stp.port state.stp.lag"]).then(function(r) {
        var o = (parseLines(r.out[0] || []).state || {}).stp || {};
        [
            ["", o.port],
            ["L", o.lag]
        ].forEach(function(g) {
            for (var n in g[1] || {}) {
                var v = g[1][n],
                    p = g[0] + n;
                if (!$("scrx" + p)) continue;
                $("scrx" + p).textContent = v.rx_bpdu;
                $("sctx" + p).textContent = v.tx_bpdu;
                $("sctrx" + p).textContent = v.rx_tc;
                $("scttx" + p).textContent = v.tx_tc;
                $("scage" + p).textContent = +v.rx_bpdu ? Math.floor(v.bpdu_age_ms / 1000) : "-";
            }
        });
    });
}

/* /stp.json the tree as it runs; get -c stp the fields, running's
 * enabled, version, priority and p2p for the status */
function stpLoad() {
    return Promise.all([getJSON("/stp.json"), cli(["get -c stp", "get stp.enabled stp.version stp.priority stp.port.*.p2p stp.lag.*.p2p", "diff"])]).then(function(x) {
        var s = x[0],
            u = parseLines(x[1].out[1] || []).stp || {};
        STPC = parseLines(x[1].out[0] || []).stp || {};
        s.on = u.enabled === "on";
        s.type = u.version;
        s.prio = +u.priority;
        s.ports.forEach(stpEnt);
        s.ports.sort(function(a, b) {
            return (a.lag - b.lag) || (a.p - b.p)
        });
        var sig = s.ports.map(function(pt) {
            return stpKey(pt) + (pt.hasOwnProperty("mbr") && pt.mbr.join(",") || "");
        }).join();
        if (sig !== stpSig) {
            stpSig = sig;
            stpBuild(s.ports);
        }
        stpCur = s;
        var en = $("stpen");
        if (document.activeElement !== en) en.checked = STPC.enabled === "on";
        var msg;
        if (!s.on) msg = t("stp_off_msg");
        else {
            var me = (s.prio * 4096).toString(16).padStart(4, "0") + s.myMac,
                tc = parseInt(s.tc),
                rp = "-";
            s.ports.forEach(function(pt) {
                if (pt.role === 1) rp = stpName(pt)
            });
            msg = t("stp_bridge") + " " + fmtBridge(me) + (s.weRoot ?
                    " (" + t("stp_root_self") + ")" :
                    "; " + t("stp_root") + " " + fmtBridge(s.rootPrio + s.rootMac) + " " + t("stp_via") + " " + rp + ", " + t("stp_cost") + " " + parseInt(s.cost)) +
                "; " + t("stp_tc") + ": " + tc + (tc ? " (" + t("stp_lasttc") + " " + stpDhms(parseInt(s.tcs)) + ")" : "") +
                "; " + t("stp_times") + " " + s.rMaxage + " / " + s.rFwd + " s";
        }
        $("stpids").textContent = msg;
        var bc = $("stpbridge"),
            df = (x[1].out[2] || []).join("\n") + "\n";	/* staged: diff's "- path v" lines */
        bc.classList.toggle("stg", /^. stp\.[a-z_]+ /m.test(df));
        if (!bc.dataset.dirty) stpPut(STPB, "", STPC);
        s.ports.forEach(function(pt) {
            var p = stpKey(pt);
            var trip = pt.f & STP_PF.TRIP;
            $("stro" + p).textContent = s.on && pt.role ? t("stp_r" + pt.role) : "-";
            $("stst" + p).innerHTML = s.on ? (pt.lk ? badge(t("stp_s" + pt.st), pt.st === 3 ? "ok" : "") : badge(t("stp_nolink"))) + (trip ? " " + badge(t("stp_trip"), "bad") : "") : "-";
            if (s.on && trip) $("stst" + p).appendChild(h("button", {
                class: "ctl",
                text: t("stp_unguard"),
                onclick: function() {
                    stpDo(p, "clear_guard")
                }
            }));
            $("stdb" + p).textContent = s.on ? fmtBridge(pt.db) : "-";
            $("stdp" + p).textContent = s.on ? parseInt(pt.dp.slice(0, 2), 16) + "." + parseInt(pt.dp.slice(2), 16) : "-";
            $("stdc" + p).textContent = s.on ? parseInt(pt.dc) : "-";
            $("stoe" + p).textContent = s.on ? t((pt.f & STP_PF.OPEREDGE) ? "c_yes" : "c_no") : "-";
            $("stop" + p).textContent = s.on ? t(stpObj(p, u).p2p === "off" ? "c_no" : "c_yes") : "-";
            $("stpv" + p).textContent = s.on && pt.role ? (s.type == "rstp" && !pt.lg ? "RSTP" : "STP") : "-";
            $("stmc" + p).disabled = !(s.on && pt.role && s.type == "rstp");
            var row = $("sten" + p).closest("tr");
            row.classList.toggle("stg", df.indexOf(" " + stpPath(p) + ".") >= 0);
            if (row.dataset.dirty) return;
            stpPut(STPF, p, stpObj(p, STPC));
        });
        if ($("stpcntd").open) return stpCnt();
    }).catch(function() {});
}

function stpApplyPort(p) {
    var cost = $("stco" + p).value;
    if (!/^\d+$/.test(cost) || +cost > 200000000) {
        toast(t("stp_cost_err"), "err");
        return;
    }
    stpStage(STPF, p, stpPath(p), stpObj(p, STPC));
    delete $("sten" + p).closest("tr").dataset.dirty;	/* after: stgBusy's blur is a change */
}
/* set lines into the candidate, then the bar's Apply; a refused set
 * stages undo, this batch's old values */
function omSet(l, undo) {
    if (stgBusy()) return Promise.resolve();
    return cli(l).then(function(r) {
        if (!r.errs.length) return stgApply();
        toast(r.errs[0], "err");
        return cli(undo);
    }).catch(function() {});
}

$("stpbridge").addEventListener("change", function() {
    this.dataset.dirty = "1"
});
$("stpbapply").addEventListener("click", function() {
    stpStage(STPB, "", "stp", STPC);
    delete $("stpbridge").dataset.dirty;
});
$("stpen").addEventListener("change", function() {
    var el = this,
        want = el.checked;
    el.checked = !want;
    confirmModal(t(want ? "stp_en_q" : "stp_dis_q"), t(want ? "stp_en_d" : "stp_dis_d"),
        function() {
            omSet(["set stp.enabled " + (want ? "on" : "off")], ["set stp.enabled " + (want ? "off" : "on")]).then(stpLoad)
        });
});
(function() {
    var sel = $("stpprio");
    for (var i = 0; i < 16; i++) sel.appendChild(h("option", {
        value: String(i),
        text: i + " (" + (i * 4096) + ")"
    }));
})();
$("stpcntd").addEventListener("toggle", function() {
    if (this.open) stpLoad()
});
/* on and stp from /loop.json (config, until P4-5), the ports from state.loop */
function loopLoad() {
    var s;
    return getJSON("/loop.json").then(function(x) {
        s = x;
        return stObj("state.loop");
    }).then(function(l) {
        var en = $("loopen");
        if (document.activeElement !== en) en.checked = s.on;
        var txt = t("loop_off"),
            lp = l.looped && l.looped !== "none" ? l.looped : "",
            bl = l.blocked && l.blocked !== "none" ? l.blocked : "";
        if (s.on && !lp)
            txt = t("loop_none");
        else if (s.on) {
            txt = t("loop_looped", {p: lp});
            if (bl)
                txt += ", " + t("loop_blocked", {p: bl});
            if (s.stp)
                txt += " (" + t("loop_stp") + ")";
        }
        $("loopst").textContent = txt;
    }).catch(function() {});
}

$("loopen").addEventListener("change", function() {
    postCmd("loop " + (this.checked ? "on" : "off")).then(loopLoad).catch(function() {});
});

var stpPoller = new Poller(function() {
    loopLoad();
    return stpLoad();
}, 3000);
tabHooks.stp = {
    enter: function() {
        stpPoller.start()
    },
    leave: function() {
        stpPoller.stop()
    }
};

/* state.port.N.mib.K (P5-5) into [name, value] rows: schema.js names each
 * index one 64-bit counter, or [high, low] two 32-bit ones sharing it */
function mibRows(v) {
    var out = [];
    SCHEMA["state.port.mib"].n.forEach(function(nm, k) {
        var w = BigInt(v[k] || 0);
        if (typeof nm === "string") out.push([nm, w]);
        else {
            if (nm[0]) out.push([nm[0], w >> 32n]);
            if (nm[1]) out.push([nm[1], w & 4294967295n]);
        }
    });
    return out;
}
var ctrPoll = null;

function showCounters(i) {
    var body = h("div");
    var bar = h("div", {
        style: "display:flex;gap:12px;align-items:center;margin-bottom:10px"
    });
    var nz = h("input", {
        type: "checkbox",
        checked: ""
    });
    var auto = h("input", {
        type: "checkbox"
    });
    bar.appendChild(h("label", null, [nz, document.createTextNode(" " + t("st_nonzero"))]));
    bar.appendChild(h("label", null, [auto, document.createTextNode(" " + t("st_autoref"))]));
    var wrap = h("div", {
        class: "scrollx"
    });
    body.appendChild(bar);
    body.appendChild(wrap);

    function load() {
        return cli(["get state.port." + (i + 1) + ".mib"]).then(function(r) {
            var rows = mibRows(((((parseLines(r.out[0] || []).state || {}).port || {})[i + 1] || {}).mib) || {});
            var tb = h("table", {
                class: "t"
            });
            tb.appendChild(h("tr", null, [h("th", {
                text: t("st_counter")
            }), h("th", {
                class: "num",
                text: t("st_value")
            })]));
            rows.forEach(function(r) {
                if (nz.checked && r[1] === 0n) return;
                tb.appendChild(h("tr", null, [h("td", {
                    text: r[0]
                }), h("td", {
                    class: "num mono",
                    text: r[1].toString()
                })]));
            });
            wrap.innerHTML = "";
            wrap.appendChild(tb);
        });
    }
    nz.addEventListener("change", load);
    if (ctrPoll) ctrPoll.stop();
    ctrPoll = new Poller(function() {
        return auto.checked ? load() : Promise.resolve()
    }, 2500);
    ctrPoll.start();
    load().catch(function() {
        wrap.textContent = t("st_fail")
    });
    modal(t("c_port") + " " + (i + 1) + ": " + t("st_counters"), body,
        [h("button", {
                class: "ctl",
                text: t("c_refresh"),
                onclick: function() {
                    load()
                }
            }),
            h("button", {
                class: "ctl pri",
                text: t("c_close"),
                onclick: function() {
                    ctrPoll.stop();
                    closeModal()
                }
            })
        ]);
}

function statsStatus() {
    var tb = $("stable").tBodies[0];
    if (tb.rows.length !== S.n) {
        tb.innerHTML = "";
        for (var i = 0; i < S.n; i++)(function(i) {
            var tr = tb.insertRow();
            for (var c = 0; c < 7; c++) tr.insertCell().className = c >= 3 ? "num" : "";
            tr.insertCell().appendChild(h("button", {
                class: "ctl",
                text: t("st_details"),
                onclick: function() {
                    showCounters(i)
                }
            }));
        })(i);
    }
    S.ports.forEach(function(p) {
        var r = tb.rows[p.port - 1];
        if (!r) return;
        r.cells[0].textContent = p.port;
        r.cells[1].textContent = p.name || "";
        r.cells[2].innerHTML = linkBadge(p);
        r.cells[3].textContent = BigInt(p.txG).toString();
        r.cells[4].textContent = BigInt(p.txB).toString();
        r.cells[5].textContent = BigInt(p.rxG).toString();
        r.cells[6].textContent = BigInt(p.rxB).toString();
    });
}
tabHooks.stats = {
    leave: function() {
        if (ctrPoll) ctrPoll.stop()
    },
    status: statsStatus,
};

function rangeStr(list) {
    if (!list.length) return "-";
    var parts = [],
        s = list[0],
        e = list[0];
    for (var i = 1; i <= list.length; i++) {
        if (list[i] === e + 1) {
            e = list[i];
            continue
        }
        parts.push(s === e ? String(s) : s + "-" + e);
        s = e = list[i];
    }
    return parts.join(", ");
}

/* VLANs (P4-2): VL the candidate's vlan.V, VR running's, VP each port's
 * PVID, VMG mgmt.vlan, VV the VLAN in the editor; the editor stages set
 * lines and applies them with the bar's window (a VLAN change can cut the
 * browser); a row that differs from running is marked staged */
var VL = {},
    VR = {},
    VP = [],
    VMG = "",
    VV = 0;

function vlanRefresh() {
    var tb = $("vtable").tBodies[0];
    tb.style = "cursor:pointer;"
    return cli(["get -c vlan", "get -c port.*.pvid", "get -c mgmt.vlan", "get vlan", "get -c lag"]).then(function(r) {
        VL = parseLines(r.out[0] || []).vlan || {};
        lagMembers(parseLines(r.out[4] || []).lag);
        VR = parseLines(r.out[3] || []).vlan || {};
        var pv = parseLines(r.out[1] || []).port || {};
        VP = [];
        for (var p in pv) VP[p] = pv[p].pvid;
        VMG = (parseLines(r.out[2] || []).mgmt || {}).vlan;
        $("vempty").style.display = Object.keys(VL).length ? "none" : "";
        tb.innerHTML = "";
        Object.keys(VR).forEach(function(id) {	/* deleted, not yet applied */
            if (VL[id]) return;
            var tr = tb.insertRow();
            tr.className = "stg";
            tr.style.textDecoration = "line-through";
            [id, VR[id].name, VR[id].members, VR[id].tagged].forEach(function(x) {
                tr.insertCell().textContent = x || "";
            });
        });
        Object.keys(VL).forEach(function(id) {
            var v = VL[id],
                w = VR[id] || {},
                m = pmParse(v.members, S.n),
                tg = pmParse(v.tagged, S.n),
                u = {},
                pp = {};
            for (var p = 1; p <= S.n; p++) {
                if (m[p] && !tg[p]) u[p] = 1;
                if (VP[p] == id) pp[p] = 1;
            }
            var tr = tb.insertRow();
            tr.className = id == VV ? "sel" : "";
            tr.classList.toggle("stg", v.name !== w.name || v.members !== w.members || v.tagged !== w.tagged);
            tr.onclick = function() {
                vlanLoad(id)
            };
            [id + (VMG == id ? " (" + t("v_mgmt") + ")" : ""), v.name, v.members, v.tagged, pmText(u, S.n), pmText(pp, S.n)].forEach(function(x) {
                tr.insertCell().textContent = x === "none" ? "-" : x || "";
            });
            var act = tr.insertCell();
            act.onclick = function(e) {
                e.stopPropagation()
            };
            act.appendChild(h("button", {
                class: "ctl",
                text: t("v_rename"),
                title: t("v_rename_t"),
                onclick: function() {
                    modal("VLAN " + id, h("label", {
                        class: "row"
                    }, [h("span", {
                        class: "lb",
                        text: t("c_name")
                    }), h("input", {
                        id: "nvlrename",
                        class: "in",
                        size: "12",
                        maxlength: "15",
                        value: v.name || ""
                    })]), [h("button", {
                        class: "ctl pri",
                        text: t("v_rename_t"),
                        onclick: function() {
                            var n = $("nvlrename").value.trim();
                            if (!validVlanName(n)) return;
                            closeModal();
                            vlanStage([n ? "set vlan." + id + ".name " + n : "del vlan." + id + ".name"]);
                        }
                    })]);
                },
            }));
            if (VMG == id) act.appendChild(h("span", {
                title: t("v_mgmt_tt"),
                class: "hint",
                style: "color: var(--fg2); font-size: 150%; padding-left: 10px;",
                text: "\u{1F6C8}"
            }));
            else act.appendChild(h("button", {
                class: "ctl",
                text: "\u2715",
                title: t("v_del_t"),
                style: "margin-left: 5px;",
                onclick: function() {
                    confirmModal(t("v_del_q", {
                        n: id
                    }), t("v_del_d"), function() {
                        /* in the same apply, the PVIDs that name it go to 0 */
                        var l = ["del vlan." + id];
                        for (var p = 1; p <= S.n; p++)
                            if (VP[p] == id) l.push("set port." + p + ".pvid 0");
                        vlanStage(l);
                    });
                }
            }));
        });
    });
}

/* the candidate as it was before lines l (VL, VP, VMG) */
function vlanUndo(l) {
    var u = [];
    l.forEach(function(x) {
        var w = x.split(" "),
            k = w[1].split("."),
            v = VL[k[1]];
        if (k[0] === "port") u.push("set " + w[1] + " " + VP[k[1]]);
        else if (k[0] === "mgmt") u.push("set mgmt.vlan " + VMG);
        else if (!v || v.fresh) u.push("del vlan." + k[1]);
        else ["name", "members", "tagged"].forEach(function(f) {
            u.push(v[f] ? "set vlan." + k[1] + "." + f + " " + v[f] : "del vlan." + k[1] + "." + f);
        });
    });
    return cli(u);
}

/* lines into the candidate, then the bar's Apply; a refused set takes
 * this batch out again (not anyone else's staging); a refused apply stays
 * staged, its rows marked */
function vlanStage(l) {
    return (stgBusy() ? Promise.resolve() : cli(l).then(function(r) {
        if (r.errs.length) {
            toast(r.errs[0], "err");
            return vlanUndo(l);
        }
        return stgApply();
    })).catch(function() {}).then(vlanRefresh).then(function() {
        if (VV) vlanLoad(VV);
    });
}

function buildVlanEdit() {
    var tb = $("vedit").tBodies[0];
    if (tb.rows.length || !S.n) return;
    var hd = tb.insertRow();
    hd.insertCell().className = "mut";
    var rN = tb.insertRow();
    rN.insertCell().textContent = t("c_name");
    var rM = tb.insertRow();
    rM.insertCell().textContent = t("v_member");
    var rP = tb.insertRow();
    rP.insertCell().textContent = t("v_pvid");
    for (var p = 1; p <= S.n; p++)(function(p) {
        hd.insertCell().innerHTML = '<b>' + p + '</b>';
        var seg = h("span", {
            class: "seg",
            id: "vm" + p
        });
        ["-", "U", "T"].forEach(function(s, ix) {
            seg.appendChild(h("button", {
                text: s,
                "data-v": ix,
                onclick: function() {	/* a LAG's members as one */
                    for (var q in LGM[p] || {}) segSet(+q, ix);
                    segSet(p, ix)
                }
            }));
        });
        seg.children[0].classList.add("on");
        rN.insertCell().appendChild(document.createTextNode((S.ports.filter(x => x.port == p)[0]).name));
        rM.insertCell().appendChild(seg);
        rP.insertCell().appendChild(h("span", {
            id: "vp" + p
        }));
    })(p);
}

function segVal(p) {
    return Number($("vm" + p).querySelector("button.on").getAttribute("data-v"));
}

function segSet(p, v) {
    $("vm" + p).querySelectorAll("button").forEach(function(b, i) {
        b.classList.toggle("on", i === v)
    });
}

/* the editor on vid: vlan.V, or a new one (VL has it locally until apply) */
function vlanLoad(vid) {
    var d = VL[vid];
    if (!d) {
        toast(t("v_notfound", {
            n: vid
        }), "err");
        return;
    }
    var m = pmParse(d.members, S.n),
        tg = pmParse(d.tagged, S.n);
    VV = vid;
    $("vtitle").innerText = "VLAN " + vid + (d.name ? " (" + d.name + ")" : "");
    for (var p = 1; p <= S.n; p++) {
        segSet(p, tg[p] ? 2 : m[p] ? 1 : 0);
        $("vp" + p).innerText = VP[p] == vid ? "Y" : "N";
    }
    $("vmgmtpromote").checked = VMG == vid;
    for (const r of $("vtable").tBodies[0].rows) r.classList.toggle("sel", r.cells[0].textContent.split(" ")[0] == vid);
}

/* the editor's VLAN as set lines: a port untagged here leaves the other
 * VLANs it is untagged in and takes this PVID; one that isn't, whose PVID
 * is this, parks on 0 */
function vlanApply() {
    var V = VV,
        m = {},
        tg = {},
        l = [];
    if (!V) return;
    if (stgBusy()) return vlanLoad(V);	/* the candidate's, as before the edit */
    for (var p = 1; p <= S.n; p++) {
        var sv = segVal(p);
        if (sv) m[p] = 1;
        if (sv === 2) tg[p] = 1;
    }
    if (!Object.keys(m).length) {
        toast(t("v_nomember"), "err");
        return;
    }
    for (var w in VL) {
        if (w == V) continue;
        var wm = pmParse(VL[w].members, S.n),
            wt = pmParse(VL[w].tagged, S.n),
            ch = 0;
        for (p = 1; p <= S.n; p++)
            if (m[p] && !tg[p] && wm[p] && !wt[p]) {
                delete wm[p];
                ch = 1;
            }
        if (ch) l.push("set vlan." + w + ".members " + pmText(wm, S.n));
    }
    if (VL[V].name && VL[V].fresh) l.push("set vlan." + V + ".name " + VL[V].name);
    l.push("set vlan." + V + ".members " + pmText(m, S.n), "set vlan." + V + ".tagged " + pmText(tg, S.n));
    var np = [],
        mv = {};
    for (p = 1; p <= S.n; p++) {	/* a LAG's members take its lowest member's, as lagApply */
        np[p] = m[p] && !tg[p] ? V : VP[p] == V ? 0 : VP[p];
        for (var q in LGM[p] || {}) {
            if (+q < p && np[p] != np[q]) {
                np[p] = np[q];
                (mv[q] = mv[q] || {})[p] = 1;
            }
            break;
        }
        if (np[p] != VP[p]) l.push("set port." + p + ".pvid " + np[p]);
    }
    for (q in mv) toast(t("lag_pvid_moved", {
        p: pmText(mv[q], S.n),
        v: np[q]
    }), "ok");
    if ($("vmgmtpromote").checked && VMG != V) l.push("set mgmt.vlan " + V);
    vlanStage(l);
}
$("vapply").addEventListener("click", vlanApply);
$("vlan_add").addEventListener("click", function() {
    modal(t("v_add"), h("div", null, [h("label", {
        class: "row"
    }, [h("span", {
        class: "lb",
        text: t("v_vid")
    }), h("input", {
        id: "nvlid",
        class: "in sm",
        type: "number",
        min: "1",
        max: "4094"
    })]), h("label", {
        class: "row"
    }, [h("span", {
        class: "lb",
        text: t("c_name")
    }), h("input", {
        id: "nvlname",
        class: "in",
        size: "12",
        maxlength: "15"
    })])]), [h("button", {
        class: "ctl pri",
        text: t("v_add"),
        onclick: addVlan
    })]);
});

function validVlanName(n) {
    if (n && !/^[a-zA-Z][!#-[\]-~]{0,14}$/.test(n)) {	/* as om_check_vlan and Str(15) */
        toast(t("v_name_rule"), "err");
        return false;
    }
    return true;
}

/* a new VLAN goes into the editor; Apply creates it */
function addVlan() {
    var vid = parseInt($("nvlid").value, 10),
        name = $("nvlname").value.trim();
    if (!(vid >= 1 && vid <= 4094) || VL[vid]) {
        toast(t("v_vid_err"), "err");
        return;
    }
    if (!validVlanName(name)) return;
    closeModal();
    VL[vid] = {
        name: name,
        members: "none",
        tagged: "none",
        fresh: 1
    };
    vlanLoad(vid);
}
tabHooks.vlan = {
    enter: function() {
        needPorts(function() {
            buildVlanEdit();
            vlanRefresh().catch(function() {}).then(function() {
                VlanInit();
            })
        })
    },
};

function VlanInit() {
    var k = Object.keys(VL);
    if (k.length) vlanLoad(VV && VL[VV] ? VV : k[0]);
}
var l2Rows = [],
    l2SortCol = "pport",
    l2SortDir = 1,
    l2Gen = 0;

/* the MAC table, K the table index: get state.l2.K- is a page of up to 25
 * (OM_PAGE) entries at or after K; K = 0, then the last + 1, until a page is
 * short. A console line waits a page, not the whole table */
function l2Load() {
    var gen = ++l2Gen,
        all = [];

    function page(k) {
        return cli(["get state.l2." + k + "-"]).then(function(r) {
            if (gen !== l2Gen) throw new Error("stale");
            if (r.errs.length) throw new Error(r.errs[0]);
            var o = (parseLines(r.out[0] || []).state || {}).l2 || {},
                n = 0;
            for (var i in o) {
                var e = o[i];
                e.idx = +i;
                e.vlan = +e.vlan;
                e.lag = +e.lag;
                e.stat = e.static === "on";
                e.pport = e.port === "cpu" ? "CPU" : e.port;
                e.where = e.lag ? "LAG" + e.lag : String(e.pport);
                all.push(e);
                n++;
            }
            return n < 25 || e.idx >= 4095 ? all : page(e.idx + 1);	/* 4095: the table's last index */
        });
    }
    return page(0);
}

function l2Key(e) {
    if (l2SortCol === "pport") {
        if (e.pport === "CPU") return 1e6;
        return e.lag ? 1000 + e.lag : Number(e.pport);
    }
    if (l2SortCol === "vlan") return e.vlan;
    if (l2SortCol === "stat") return e.stat ? 1 : 0;
    return e.mac;
}

function l2Fetch() {
    if (stgHold) return Promise.resolve();	/* in an apply window: the table as it was, not a 3 s cut */
    $("l2count").textContent = t("l2_loading");
    return l2Load().then(function(all) {
        l2Rows = all;
        l2Render();
    }).catch(function(e) {
        if (e.message !== "stale") $("l2count").textContent = t("l2_failed")
    });
}

function l2Render() {
    var f = $("l2filter").value.toLowerCase();
    var tb = $("l2table").tBodies[0];
    tb.innerHTML = "";
    var rows = l2Rows.slice().sort(function(a, b) {
        var x = l2Key(a),
            y = l2Key(b);
        return ((x > y) - (x < y)) * l2SortDir || ((a.mac > b.mac) - (a.mac < b.mac));
    });
    $("l2table").querySelectorAll("th[data-sort]").forEach(function(th) {
        th.querySelector(".arrow").textContent = th.dataset.sort === l2SortCol ? (l2SortDir > 0 ? " \u25b2" : " \u25bc") : "";
    });
    var shown = 0;
    rows.forEach(function(e) {
        var ty = t(e.stat ? "l2_static" : "l2_learned");
        var hay = (e.mac + " " + e.vlan + " " + e.where + " " + ty).toLowerCase();
        if (f && hay.indexOf(f) < 0) return;
        shown++;
        var tr = tb.insertRow();
        tr.insertCell().textContent = e.where;
        tr.insertCell().className = "mono";
        tr.cells[1].textContent = e.mac;
        tr.insertCell().textContent = e.vlan;
        tr.insertCell().textContent = ty;
        var dc = tr.insertCell();
        if (e.pport !== "CPU") dc.appendChild(h("button", {
            class: "ctl",
            text: "\u2715",
            title: t("l2_del_t"),
            onclick: function() {
                postJSON("/l2_del.json?idx=" + e.idx).then(function() {
                    l2Rows = l2Rows.filter(function(x) {
                        return x !== e
                    });
                    l2Render();
                }).catch(function() {});
            }
        }));
    });
    $("l2count").textContent = shown + " / " + l2Rows.length + " " + t("l2_entries");
}
$("l2table").querySelectorAll("th[data-sort]").forEach(function(th) {
    th.addEventListener("click", function() {
        var c = th.dataset.sort;
        l2SortDir = (c === l2SortCol) ? -l2SortDir : 1;
        l2SortCol = c;
        l2Render();
    });
});
$("l2filter").addEventListener("input", l2Render);
$("l2refresh").addEventListener("click", function() {
    l2Fetch()
});
$("l2flush").addEventListener("click", function() {
    confirmModal(t("l2_flush_q"), "", function() {
        postCmd("l2 forget").then(function() {
            setTimeout(l2Fetch, 500)
        }).catch(function() {});
    });
});
tabHooks.l2 = {
    enter: function() {
        needPorts(function() {
            l2Fetch()
        })
    },
    leave: function() {
        l2Gen++
    }
};

function buildMirror() {
    var sel = $("mport");
    if (sel.options.length) return;
    for (var p = 1; p <= S.n; p++) sel.appendChild(h("option", {
        value: p,
        text: t("c_port") + " " + p
    }));
    var tb = $("mtable").tBodies[0];
    var hd = tb.insertRow();
    hd.insertCell().className = "mut";
    var r = tb.insertRow();
    r.insertCell().textContent = t("m_mirror");
    for (var q = 1; q <= S.n; q++)(function(q) {
        hd.insertCell().innerHTML = "<b>" + q + "</b>";
        var seg = h("span", {
            class: "seg",
            id: "mm" + q
        });
        ["-", "RX", "TX", t("m_both")].forEach(function(s, ix) {
            seg.appendChild(h("button", {
                text: s,
                "data-v": ix,
                onclick: function() {
                    seg.querySelectorAll("button").forEach(function(b) {
                        b.classList.remove("on")
                    });
                    this.classList.add("on");
                }
            }));
        });
        seg.children[0].classList.add("on");
        r.insertCell().appendChild(seg);
    })(q);
}

function mirrorLoad() {
    return getJSON("/mirror.json").then(function(m) {
        $("mstate").textContent = t(m.enabled ? "m_active" : "c_off");
        $("mstate").className = "badge " + (m.enabled ? "ok" : "");
        if (m.enabled) $("mport").value = m.mirrorPort;
        for (var p = 1; p <= S.n; p++) {
            var v = (m.mirror_rx.includes(p) ? 1 : 0) + (m.mirror_tx.includes(p) ? 2: 0);
            $("mm" + p).querySelectorAll("button").forEach(function(b, i) {
                b.classList.toggle("on", i === v)
            });
        }
    });
}
$("mapply").addEventListener("click", function() {
    var mp = $("mport").value,
        cmd = "mirror " + mp,
        any = false;
    for (var p = 1; p <= S.n; p++) {
        if (String(p) === mp) continue;
        var v = Number($("mm" + p).querySelector("button.on").getAttribute("data-v"));
        if (v === 1) {
            cmd += " " + p + "r";
            any = true;
        } else if (v === 2) {
            cmd += " " + p + "t";
            any = true;
        } else if (v === 3) {
            cmd += " " + p;
            any = true;
        }
    }
    if (!any) {
        toast(t("m_none"), "err");
        return;
    }
    postCmd(cmd).then(mirrorLoad).catch(function() {});
});
$("moff").addEventListener("click", function() {
    postCmd("mirror off").then(mirrorLoad).catch(function() {});
});
tabHooks.mirror = {
    enter: function() {
        needPorts(function() {
            buildMirror();
            mirrorLoad().catch(function() {})
        })
    }
};

var HASHF = ["spa", "smac", "dmac", "sip", "dip", "sport", "dport"],
    NLAG = +SCHEMA.lag.i.slice(1);	/* "N4" */

/* the candidate's LAGs that aren't off (apply's check): LGM[p] the ports
 * of p's LAG; a PVID or a VLAN membership moves them as one */
var LGM = {};

function lagMembers(o) {
    LGM = {};
    for (var g in o || {})
        if (o[g].mode !== "off") {
            var m = pmParse(o[g].members, S.n);
            for (var p in m) LGM[p] = m;
        }
}

function buildLag() {
    var w = $("lagwrap");
    if (w.children.length) return;
    for (var g = 1; g <= NLAG; g++)(function(g) {
        var card = h("div", { class: "card" });
        card.appendChild(h("h2", { text: "LAG " + g }));
        var mr = h("div", { style: "display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-bottom:10px", class: "small" });
        mr.appendChild(h("span", { class: "mut", text: t("lag_mode") }));
        var ms = h("select", { class: "in", id: "lgm" + g });
        ms.appendChild(h("option", { value: "off", text: t("c_offc") }));
        ms.appendChild(h("option", { value: "static", text: t("lag_static") }));
        ms.appendChild(h("option", { value: "lacp", text: "LACP" }));
        mr.appendChild(ms);
        card.appendChild(mr);
        card.appendChild(h("p", { class: "small mono", id: "lgs" + g, style: "margin-bottom:10px" }));
        var pr = h("div", { style: "display:flex;gap:10px;flex-wrap:wrap;margin-bottom:10px" });
        for (var p = 1; p <= S.n; p++) pr.appendChild(h("label", null, [
            h("input", {
                type: "checkbox",
                id: "lg" + g + "p" + p
            }), document.createTextNode(" " + p + " "),
        ]));
        card.appendChild(pr);
        var hr = h("div", { style: "display:flex;gap:10px;flex-wrap:wrap;margin-bottom:10px", class: "small" });
        hr.appendChild(h("span", { class: "mut", text: t("lag_hash") }));
        HASHF.forEach(function(f) {
            hr.appendChild(h("label", null, [h("input", {
                type: "checkbox",
                id: "lg" + g + "h" + f
            }), document.createTextNode(" " + f + " ")]));
        });
        card.appendChild(hr);
        card.appendChild(h("button", { class: "ctl pri", text: t("c_apply"),
            onclick: function() {
                lagApply(g)
            }
        }));
        card.addEventListener("change", function() { card.dataset.dirty = "1" });
        w.appendChild(card);
    })(g);
}

/* lag.L (P4-3): LC the candidate's, a card staged while it differs from
 * running; state.lag.L and state.lacp.port.N below */
var LC = {};

function lagLoad() {
    return cli(["get -c lag", "get lag", "get state.lag", "get state.lacp.port"]).then(function(r) {
        LC = parseLines(r.out[0] || []).lag || {};
        lagMembers(LC);
        var run = parseLines(r.out[1] || []).lag || {},
            st = parseLines((r.out[2] || []).concat(r.out[3] || [])).state || {},
            sl = st.lag || {},
            lp = (st.lacp || {}).port || {};
        for (var g = 1; g <= NLAG; g++) {
            var c = LC[g] || {},
                u = run[g] || {},
                x = sl[g] || {},
                card = $("lgm" + g).closest(".card");
            card.classList.toggle("stg", c.mode !== u.mode || c.members !== u.members || c.hash !== u.hash);
            $("lgs" + g).textContent = c.mode === "lacp" ? "LACP: " + t("lacp_agg") + " " + (x.agg_valid === "on" ? x.aggregator : "(" + t("lacp_neg") + ")") +
                ", " + t("lacp_members") + " " + x.active : c.mode === "static" ? t("lacp_members") + " " + x.active : "";
            if (card.dataset.dirty) continue;
            var m = pmParse(c.members, S.n),
                hs = (c.hash || "").split(",");
            $("lgm" + g).value = c.mode;
            for (var p = 1; p <= S.n; p++) $("lg" + g + "p" + p).checked = !!m[p];
            HASHF.forEach(function(f) {
                $("lg" + g + "h" + f).checked = hs.indexOf(f) >= 0
            });
        }
        var tb = $("lacptbl").tBodies[0];
        tb.innerHTML = "";
        Object.keys(lp).forEach(function(p) {
            var q = lp[p];
            if (q.lag === "0") return;
            var tr = tb.insertRow();
            [p, q.lag, q.actor, q.partner, q.rx_state, q.rx_pdus, q.partner_sys].forEach(function(v) {
                tr.insertCell().textContent = v
            });
        });
    });
}

/* card g as set lines, its members' PVIDs made one (the lowest member's,
 * said in a toast) as apply's check wants; no members clears it */
function lagApply(g) {
    var mode = $("lgm" + g).value,
        m = {},
        hs = HASHF.filter(function(f) {
            return $("lg" + g + "h" + f).checked
        }),
        own = pmParse((LC[g] || {}).members, S.n);
    if (stgBusy()) return lagStage(g, [], {});	/* the card back to the candidate */
    for (var p = 1; p <= S.n; p++)
        if ($("lg" + g + "p" + p).checked) {
            if (LGM[p] && !own[p]) return toast(t("lag_other", {	/* apply's "one lag" */
                p: p
            }), "err");
            m[p] = 1;
        }
    if (!Object.keys(m).length) return confirmModal(t("lag_clear_q", {
        n: g
    }), t("lag_clear_d"), function() {
        lagStage(g, ["del lag." + g], {});
    });
    if (!hs.length) return toast(t("lag_hash_none"), "err");
    cli(["get -c port.*.pvid"]).then(function(r) {
        var pv = parseLines(r.out[0] || []).port || {},
            l = ["set lag." + g + ".mode " + mode, "set lag." + g + ".members " + pmText(m, S.n), "set lag." + g + ".hash " + hs.join(",")],
            old = {},
            mv = {},
            v;
        for (var p in m) {
            if (v === undefined) v = pv[p].pvid;
            else if (mode !== "off" && pv[p].pvid !== v) {
                l.push("set port." + p + ".pvid " + v);
                old[p] = pv[p].pvid;
                mv[p] = 1;
            }
        }
        if (Object.keys(mv).length) toast(t("lag_pvid_moved", {
            p: pmText(mv, S.n),
            v: v
        }), "ok");
        lagStage(g, l, old);
    }).catch(function() {});
}

/* lines into the candidate, then the bar's Apply; a refused set puts LAG
 * g and the PVIDs old back (this batch only) */
function lagStage(g, l, old) {
    var c = LC[g] || {};
    delete $("lgm" + g).closest(".card").dataset.dirty;
    return (!l.length || stgBusy() ? Promise.resolve() : cli(l).then(function(r) {
        if (!r.errs.length) return stgApply();
        toast(r.errs[0], "err");
        var u = ["mode", "members", "hash"].map(function(f) {
            return "set lag." + g + "." + f + " " + c[f]
        });
        for (var p in old) u.push("set port." + p + ".pvid " + old[p]);
        return cli(u);
    })).catch(function() {}).then(lagLoad).catch(function() {});
}
var lagPoller = new Poller(function() {
    return lagLoad().catch(function() {})
}, 3000);
tabHooks.lag = {
    enter: function() {
        needPorts(function() {
            buildLag();
            lagPoller.start()
        })
    },
    leave: function() {
        lagPoller.stop()
    }
};

function lldpLoad() {
    return getJSON("/lldp.json").then(function(s) {
        $("lldp_en").checked = s.on;
        var tb = $("lldp_table").tBodies[0];
        tb.innerHTML = "";
        byPort(s.portStatus).forEach(function(p) {
            var tr = tb.insertRow();
            tr.insertCell().textContent = p.port;
            tr.insertCell().textContent = p.name;
            var sw = h("label", {
                class: "switch"
            }, [
                h("input", {
                    type: "checkbox",
                    onchange: function() {
                        postCmd("port " + p.port + " lldp " + (this.checked ? "permit" : "block"))
                            .then(function() {
                                setTimeout(lldpLoad, 300)
                            }).catch(function() {});
                    }
                }), h("i")
            ]);
            sw.firstChild.checked = p.permitted;
            tr.insertCell().appendChild(sw);
        });
    });
}

$("lldp_en").addEventListener("change", function() {
    var el = this;
    postCmd("lldp " + (el.checked ? "on" : "off")).catch(function() {
        el.checked = !el.checked
    });
});
tabHooks.lldp = {
    enter: lldpLoad
};

function bwLoad() {
    return getJSON("/bandwidth.json").then(function(s) {
        var tb = $("btable").tBodies[0];
        tb.innerHTML = "";
        byPort(s).forEach(function(p) {
            var n = p.port;
            var iOn = p.iLimited, eOn = p.eLimited;
            var iM = (parseInt(p.iBW) * 16 / 1000),
                eM = (parseInt(p.eBW) * 16 / 1000);
            var tr = tb.insertRow();
            tr.insertCell().textContent = n;
            var icb = h("input", { type: "checkbox", id: "bwi" + n });
            icb.checked = iOn;
            tr.insertCell().appendChild(icb);
            var iin = h("input", {
                class: "in sm",
                id: "bwiv" + n,
                type: "number",
                min: "0.016",
                max: "10000",
                step: "any"
            });
            if (iOn) iin.value = +iM.toFixed(3);
            tr.insertCell().appendChild(iin);
            var msel = h("select", { class: "in", id: "bwm" + n }, [
                h("option", { value: "fc", text: t("bw_fc") }),
                h("option", { value: "drop", text: t("bw_drop") }),
            ]);
            msel.value = p.iAction;
            tr.insertCell().appendChild(msel);
            var ecb = h("input", { type: "checkbox", id: "bwe" + n });
            ecb.checked = eOn;
            tr.insertCell().appendChild(ecb);
            var ein = h("input", {
                class: "in sm",
                id: "bwev" + n,
                type: "number",
                min: "0.016",
                max: "10000",
                step: "any"
            });
            if (eOn) ein.value = +eM.toFixed(3);
            tr.insertCell().appendChild(ein);
            tr.insertCell().appendChild(h("button", { class: "ctl", text: t("c_apply"),
                onclick: function() {
                    bwApply(n)
                }
            }));
        });
    });
}
/* The bw command takes kbit/s and converts to 16-kbit register steps itself. */
function bwHex(mbit) {
    var kbit = Math.round(mbit * 1000);
    if (kbit < 16 || kbit > 10000000) return null;
    var x = kbit.toString(16);
    return x.length % 2 ? "0" + x : x;
}

function bwApply(n) {
    var cmds = [];
    if ($("bwi" + n).checked) {
        var ih = bwHex(parseFloat($("bwiv" + n).value));
        if (!ih) {
            toast(t("bw_in_err"), "err");
            return;
        }
        cmds.push("bw in " + n + " " + ih);
        cmds.push("bw in " + n + " " + $("bwm" + n).value);
    } else cmds.push("bw in " + n + " off");
    if ($("bwe" + n).checked) {
        var eh = bwHex(parseFloat($("bwev" + n).value));
        if (!eh) {
            toast(t("bw_out_err"), "err");
            return;
        }
        cmds.push("bw out " + n + " " + eh);
    } else cmds.push("bw out " + n + " off");
    postCmds(cmds).then(bwLoad).catch(function(){});
}
var SC_TYPES = ["bcast", "mcast", "ucast", "umcast"],
    scSig = "";

function scLoad() {
    return getJSON("/storm.json").then(function(s) {
        var tb = $("sctable").tBodies[0];
        byPort(s);
        var sig = s.map(function(p) {
            return p.port
        }).join();
        if (sig !== scSig) {
            scSig = sig;
            tb.innerHTML = "";
            s.forEach(function(p) {
                var n = p.port,
                    tr = tb.insertRow();
                tr.id = "scr" + n;
                tr.addEventListener("input", function() { tr.dataset.dirty = "1" });
                tr.addEventListener("change", function() { tr.dataset.dirty = "1" });
                tr.insertCell().textContent = n;
                SC_TYPES.forEach(function(ty) {
                    tr.insertCell().appendChild(h("span", { style: "display:flex;gap:4px" }, [
                        h("input", { class: "in sm", id: "scv" + ty + n, type: "number", min: "1", step: "1" }),
                        h("select", { class: "in", id: "scu" + ty + n }, [
                            h("option", { value: "pps", text: "pps" }),
                            h("option", { value: "kbps", text: "kbit/s" }),
                        ]),
                    ]));
                });
                tr.insertCell().appendChild(h("button", {
                    class: "ctl",
                    text: t("c_apply"),
                    onclick: function() {
                        scApply(n)
                    }
                }));
            });
        }
        s.forEach(function(p) {
            var n = p.port;
            if ($("scr" + n).dataset.dirty) return;
            SC_TYPES.forEach(function(v, k) {
                var on = p[v]["enabled"],
                    pps = p[v]["unit"] == "pps",
                    raw = parseInt(p[v]["rate"]);
                var inp = $("scv" + v + n),
                    sel = $("scu" + v + n);
                inp.value = on ? raw : "";
                sel.value = on && !pps ? "kbps" : "pps";
                inp.dataset.cur = on ? inp.value + " " + sel.value : "off";
            });
        });
    });
}

function scApply(n) {
    var cmds = [];
    for (var k = 0; k < SC_TYPES.length; k++) {
        var ty = SC_TYPES[k],
            inp = $("scv" + ty + n),
            u = $("scu" + ty + n).value,
            v = inp.value.trim(),
            want = "off";
        if (v !== "" && v !== "0") {
            var x = Number(v);
            if (!Number.isInteger(x) || x < 1 || (u === "pps" && x > 1048575) || (u === "kbps" && x > 10000000)) {
                toast(t("sc_err"), "err");
                return;
            }
            want = x + " " + u;
        }
        if (want !== inp.dataset.cur) cmds.push("storm " + n + " " + ty + " " + want);
    }
    delete $("scr" + n).dataset.dirty;
    if (!cmds.length) return;
    postCmds(cmds).then(scLoad).catch(function() {});
}
tabHooks.bw = {
    enter: function() {
        needPorts(function() {
            bwLoad().then(scLoad).catch(function() {})
        })
    }
};

var IPRE = /^(\d{1,3}\.){3}\d{1,3}$/;

function okIp(s) {
    if (!IPRE.test(s)) return false;
    return s.split(".").every(function(o) {
        return +o <= 255
    });
}

/* The system tab's fields: element id -> config path (P4-1); SY their
 * candidate as get -c prints it */
var SY_F = {
        "sy-host": "sys.hostname",
        "ledSel": "sys.led",
        "sy-mode": "mgmt.mode",
        "sy-ip": "mgmt.ip",
        "sy-mask": "mgmt.netmask",
        "sy-gw": "mgmt.gateway",
        "sy-dns1": "dns.server1",
        "sy-dns2": "dns.server2",
        "sy-syslog": "syslog.enabled",
        "sy-sysip": "syslog.server",
        "sy-sysport": "syslog.port",
        "sy-sesstmo": "web.session_timeout_s",
        "sy-ntp": "ntp.enabled",
        "sy-ntpsrv": "ntp.server",
        "sy-ntpint": "ntp.interval_min",
        "sy-tz": "ntp.timezone",
        "sy-dst": "ntp.dst"
    },
    SY = {};

function syField(id) {
    var p = SY_F[id].split(".");
    return omField(p[0], p[1]);
}

/* a field's value as set takes it; an empty address is 0.0.0.0 */
function syGet(id) {
    var e = $(id),
        v = e.type === "checkbox" ? (e.checked ? "on" : "off") : e.value.trim();
    return v === "" && syField(id).t === "ip" ? "0.0.0.0" : v;
}

function syPut(id, v) {
    var e = $(id);
    if (document.activeElement === e) return;
    if (e.type === "checkbox") e.checked = v === "on";
    else e.value = v === "0.0.0.0" && syField(id).t === "ip" ? "" : v || "";
}

function sysLoad() {
    cli(["get -c sys mgmt dns syslog ntp web"]).then(function(r) {
        var o = parseLines(r.out[0] || []);
        for (var id in SY_F) {
            var p = SY_F[id].split(".");
            SY[id] = (o[p[0]] || {})[p[1]];
            syPut(id, SY[id]);
        }
        syModeUi();
        if (SY["sy-mode"] === "dhcp") return stObj("state.mgmt").then(function(m) {
            syPut("sy-ip", m.ip);	/* the lease, in the disabled fields */
            syPut("sy-mask", m.netmask);
            syPut("sy-gw", m.gateway);
        });
    }).catch(function() {});
    stObj("state.dns").then(function(d) {
        $("sy-dnsdhcp").textContent = !d.dhcp_server || d.dhcp_server === "0.0.0.0" ? "" : t("sy_dns_dhcp", {
            ip: d.dhcp_server
        });
    }).catch(function() {});
    cfgReload();
}

/* the fields that differ from the candidate as set (del when emptied)
 * lines; null after a refusal */
function sySets(ids) {
    var l = [];
    for (var i = 0; i < ids.length; i++) {
        var id = ids[i],
            v = syGet(id),
            why = v === "" ? "" : SY_F[id] === "sys.hostname" && /\s/.test(v) ? t("sy_host_err") : omCheck(syField(id), v);
        if (v === SY[id]) continue;
        if (why) {
            toast(SY_F[id] + ": " + why, "err");
            return null;
        }
        l.push((v === "" ? "del " : "set ") + SY_F[id] + (v === "" ? "" : " " + v));
    }
    return l;
}

/* the candidate's values back for ids (after a refused set) */
function syUndo(ids) {
    return cli(ids.filter(function(id) {
        return syGet(id) !== SY[id]
    }).map(function(id) {
        return SY[id] ? "set " + SY_F[id] + " " + SY[id] : "del " + SY_F[id]
    }));
}

/* stage a card's fields, then the bar's Apply (its window; go, tab: the
 * new address to confirm from, stgGo) */
function syApply(ids, win, go, tab) {
    var l = sySets(ids);
    if (!l || !l.length) return;
    if (stgBusy()) return sysLoad();
    cli(l).then(function(r) {
        if (r.errs.length) return syUndo(ids).then(function() {
            throw new Error(r.errs[0]);
        });
        return stgApply(win, go, tab);
    }).then(function(a) {
        if (a === "refused" && tab) tab.close();
    }, function(e) {
        if (tab) tab.close();
        toast(e.message, "err");
    }).then(sysLoad);
}

function cfgParseKnown(txt) {
    var igmp = false;
    txt.split(/\r?\n/).forEach(function(l) {
        l = l.trim();
        if (/^igmp on$/.test(l)) igmp = true;
        if (/^igmp off$/.test(l)) igmp = false;
    });
    $("sy-igmp").checked = igmp;
}

function syModeUi() {
    var dhcp = $("sy-mode").value === "dhcp";
    ["sy-ip", "sy-mask", "sy-gw", "sy-dns1", "sy-dns2"].forEach(function(id) {
        $(id).disabled = dhcp;
    });
}
$("sy-mode").addEventListener("change", syModeUi);
$("ledSel").addEventListener("change", function() {
    syApply(["ledSel"]);
});
/* A new static address: apply with a long window, then confirm from
 * there (DESIGN 5.3); the switch rolls back if that never happens. To
 * DHCP the address is unknown: a plain apply, no window */
$("sy-apply").addEventListener("click", function() {
    var dhcp = syGet("sy-mode") === "dhcp",
        toDhcp = dhcp && SY["sy-mode"] !== "dhcp",
        ip = syGet("sy-ip"),
        moving = !dhcp && (ip !== SY["sy-ip"] || SY["sy-mode"] === "dhcp"),
        ids = dhcp ? ["sy-host", "sy-mode"] : ["sy-host", "sy-mode", "sy-ip", "sy-mask", "sy-gw", "sy-dns1", "sy-dns2"];
    if (!sySets(ids)) return;
    confirmModal(toDhcp ? t("sy_dhcp_q") : t("sy_net_q"), toDhcp ? t("sy_dhcp_w") : moving ? t("sy_move_d", {
        ip: ip,
        old: SY["sy-ip"]
    }) : "", function() {
        var tab = moving && window.open("", "_blank");	/* from the click, or it is blocked */
        if (!toDhcp) return syApply(ids, moving ? STG_LONG : 0, moving ?
            location.protocol + "//" + ip + (location.port ? ":" + location.port : "") + "/index.html" : "", tab);
        /* a plain apply: refused, the fields go back */
        cli(sySets(ids)).then(function(r) {
            if (r.errs.length) throw new Error(r.errs[0]);
            return cli(["apply"]);
        }).then(function(r) {
            if (r.errs.length) return syUndo(ids).then(function() {
                throw new Error(r.errs[0]);
            });
        }).catch(function(e) {
            if (e.message) toast(e.message, "err");
        }).then(sysLoad);
    });
});
$("sy-igmp").addEventListener("change", function() {
    var el = this;
    postCmd("igmp " + (el.checked ? "on" : "off")).catch(function() {
        el.checked = !el.checked
    });
});
$("sy-syslog").addEventListener("change", function() {
    syApply(["sy-syslog", "sy-sysip", "sy-sysport"]);
});
$("sy-sesstmo").addEventListener("change", function() {
    syApply(["sy-sesstmo"]);
});
/* a new password logs every session out at apply: a long window, then
 * the login, where logging in with it and keeping it (stgAsk) confirms */
$("sy-pwapply").addEventListener("click", function() {
    var a = $("sy-pw1").value,
        b = $("sy-pw2").value;
    if (a.length < 1 || a.length > 20) {
        toast(t("sy_pw_len"), "err");
        return;
    }
    if (/[\s"\\]/.test(a)) {
        toast(t("sy_pw_space"), "err");
        return;
    }
    if (a !== b) {
        toast(t("sy_pw_match"), "err");
        return;
    }
    confirmModal(t("sy_pw_q"), t("sy_pw_w"), function() {
        cli(["set web.password " + a]).then(function(r) {
            if (r.errs.length) throw new Error(r.errs[0]);
            return stgApply(STG_LONG, "/login.html");
        }).then(function(a) {
            if (a === "refused") throw new Error(t("stg_unapplied"));
        }).catch(function(e) {
            /* a password must not stay staged: the old one back */
            cli(["discard"]).then(omReload);
            toast(t("stg_discarded", {
                e: e.message
            }), "err");
        });
    });
});
$("sy-send").addEventListener("click", sysConsole);
$("sy-cmd").addEventListener("keydown", function(e) {
    if (e.key === "Enter") sysConsole()
});

function sysConsole() {
    var c = $("sy-cmd").value.trim();
    if (!c) return;
    var out = $("sy-cout");
    api("/cmd", {
        method: "POST",
        body: c
    }).then(function(r) {
        var body = r.body.replace(/\s+$/, "");
        out.textContent += "> " + c + "\n";
        if (body) out.textContent += body + "\n";
        else out.textContent += (r.ok ? "OK" : "ERROR " + r.status) + "\n";
        if (out.textContent.length > 20000) out.textContent = out.textContent.slice(-16000);
        out.scrollTop = out.scrollHeight;
        if (r.ok && isConfCmd(c)) setDirty(true);
    }).catch(function(e) {
        out.textContent += "> " + c + "\n" + e + "\n"
    });
    $("sy-cmd").value = "";
}
$("sy-reboot").addEventListener("click", function() {
    confirmModal(t("sy_reboot_q"), S.dirty ? t("sy_reboot_d") : "", function() {
        api("/reset", post()).catch(function() {});
        toast(t("sy_rebooting"), "ok");
    });
});

function cfgReload() {
    return getText("/config").then(function(x) {
        x = x.replace(/\0[\s\S]*$/, "");
        $("cfgedit").value = x;
        cfgBytes();
        cfgParseKnown(x);
    }).catch(function() {});
}

function cfgLongLine(v) {
    var ls = v.split("\n");
    for (var i = 0; i < ls.length; i++) {
        var n = new Blob([ls[i]]).size;
        if (n > 126) return {
            n: i + 1,
            m: n
        };
    }
    return null;
}

function cfgBytes() {
    var v = $("cfgedit").value,
        n = new Blob([v]).size;
    var el = $("cfgbytes");
    el.textContent = n + " / 2048 " + t("sy_bytes");
    el.style.color = (n > 2048 || cfgLongLine(v)) ? "var(--bad)" : "";
    return n;
}
$("cfgedit").addEventListener("input", cfgBytes);
$("cfgreload").addEventListener("click", cfgReload);
// Download what is in flash as text; a file loaded back goes into the editor,
// to be checked and written like an edit.
$("cfgdl").addEventListener("click", function() {
    getText("/config").then(function(x) {
        var a = h("a", {
            href: URL.createObjectURL(new Blob([x.replace(/[\0\xff\ufffd][\s\S]*$/, "")], {
                type: "text/plain"
            })),
            download: (S.host || "switch") + "-config.txt"
        });
        document.body.appendChild(a);
        a.click();
        a.remove();
    }).catch(function() {});
});
$("cfgload").addEventListener("click", function() {
    $("cfgfile").click()
});
$("cfgfile").addEventListener("change", function() {
    var f = this.files[0];
    if (!f) return;
    f.text().then(function(x) {
        x = x.replace(/[\0\xff\ufffd][\s\S]*$/, "");
        $("cfgedit").value = x;
        cfgBytes();
        cfgParseKnown(x);
        toast(t("sy_loaded_note"));
    });
    this.value = "";
});
var ntpBase = null,
    ntpTimer = null;

function tzText(m) {
    var a = Math.abs(m);
    return (m < 0 ? "-" : "+") + String(Math.floor(a / 60)).padStart(2, "0") + ":" + String(a % 60).padStart(2, "0")
}

function ntpTick() {
    if (!ntpBase) return;
    var d = new Date(ntpBase.ms + (Date.now() - ntpBase.at));

    function p(n) {
        return String(n).padStart(2, "0")
    }
    $("sy-sync").textContent = t("sy_ago", {
        n: ntpBase.sync + Math.floor((Date.now() - ntpBase.at) / 60000)
    });
    $("sy-now").textContent = d.getUTCFullYear() + "-" + p(d.getUTCMonth() + 1) + "-" + p(d.getUTCDate()) + " " +
        p(d.getUTCHours()) + ":" + p(d.getUTCMinutes()) + ":" + p(d.getUTCSeconds()) + " UTC" + ntpBase.tz;
}

/* the clock from state.ntp; the config is sysLoad's */
function ntpLoad() {
    return stObj("state.ntp").then(function(n) {
        var off = +n.local_offset_min || 0;	/* DST included */
        $("sy-ntpaddr").textContent = !n.server_ip || n.server_ip === "0.0.0.0" ? "-" : n.server_ip;
        if (n.synced === "on") {
            ntpBase = {
                at: Date.now(),
                ms: (+n.time + off * 60) * 1000,
                tz: tzText(off) + (n.dst_active === "on" ? " DST" : ""),
                sync: Math.floor((+n.last_sync_s || 0) / 60)
            };
            ntpTick();
        } else {
            ntpBase = null;
            $("sy-now").textContent = t("sy_nosync");
            $("sy-sync").textContent = SY["sy-ntp"] === "on" ? t("sy_waiting") : "-";
        }
    }).catch(function() {});
}
[-720, -660, -600, -570, -540, -480, -420, -360, -300, -240, -210, -180, -120, -60, 0, 60, 120, 180, 210, 240, 270, 300, 330, 345,
    360, 390, 420, 480, 525, 540, 570, 600, 630, 660, 720, 765, 780, 840
].forEach(function(z) {
    $("sy-tz").appendChild(h("option", {
        value: tzText(z),
        text: "UTC" + tzText(z)
    }));
});
$("sy-ntpapply").addEventListener("click", function() {
    syApply(["sy-ntp", "sy-ntpsrv", "sy-ntpint", "sy-tz", "sy-dst"]);
});
$("sy-ntprefresh").addEventListener("click", ntpLoad);
tabHooks.system = {
    enter: function() {
        sysLoad();
        ntpLoad();
        clearInterval(ntpTimer);
        ntpTimer = setInterval(ntpTick, 1000)
    },
    leave: function() {
        clearInterval(ntpTimer)
    },
};

/* Save to flash: the binary save (P3B), as the staging bar's */
$("saveBtn").addEventListener("click", stgSave);

// Every image carries its PCB configuration name as a NUL-terminated string,
// the same one the running switch reports as pcb_ver: an image without it was
// built for another board (SFP, LED and button pins differ between PCBs).
// Firmware older than pcb_ver reports only the machine name as hw_ver; check
// against that instead of skipping the test.
function fwBoardName() {
    return S.sys.pcb || S.sys.machine;
}
function fwForThisSwitch(u) {
    var name = fwBoardName();
    if (!name) return true;
    var n = [];
    for (var i = 0; i < name.length; i++) n.push(name.charCodeAt(i) & 0xff);
    n.push(0);
    outer: for (var p = 0; p <= u.length - n.length; p++) {
        for (var k = 0; k < n.length; k++)
            if (u[p + k] !== n[k]) continue outer;
        return true;
    }
    return false;
}

// What a file is: "app" (an application image, or a compact install image -
// the application followed by the loader, each with its own CRC), "loader"
// (16 KiB) or "mdata" (a machine data region, "MDT1": the PoE controller's
// firmware). Returns {kind} or {msg} for a file that is none of them. The
// running firmware reports the image size its flash layout takes (compact
// builds are smaller); older firmware is 512 KiB.
function fwClassify(u) {
    var want = Number(S.sys.image_size) || 524288;
    var kind, inst = want < 524288 && u.length === want + 16384;
    if (u.length > 10 && u[0] === 0x4d && u[1] === 0x44 && u[2] === 0x54 && u[3] === 0x31) kind = "mdata";
    else if (u.length === 16384) kind = "loader";
    else if (u.length === want || inst) kind = "app";
    else return {
        msg: t("fw_size_err", {
            n: u.length,
            w: want
        })
    };
    if (kind !== "mdata" && (u[0] !== 0x00 || u[1] !== 0x40 || u[2] !== 0x02)) return {
        msg: t("fw_magic_err")
    };
    var crc = 0;
    for (var i = 0; i < u.length; i++) {
        if (inst && i === want) {
            if (crc !== 0xB001 || u[i] !== 0x00 || u[i + 1] !== 0x40) break;
            crc = 0;
        }
        crc ^= u[i];
        for (var b = 0; b < 8; b++) crc = (crc & 1) ? ((crc >>> 1) ^ 0xA001) : (crc >>> 1);
    }
    if (crc !== 0xB001 || i !== u.length) return {
        msg: t("fw_crc_err")
    };
    return {
        kind: kind,
        inst: inst
    };
}

// One upload card: p is its element id prefix, kind the only file kind it
// takes, ep where that goes.
var FW_CARDS = {
    app: {
        p: "fw",
        ep: "/upload",
        title: "fw_title",
        valid: "fw_valid",
        d: "fw_d"
    },
    mdata: {
        p: "pf",
        ep: "/mdata",
        title: "pf_title",
        valid: "fw_md_valid",
        d: "fw_md_d"
    },
    loader: {
        p: "ld",
        ep: "/loader",
        title: "ld_title",
        valid: "fw_ldr_valid",
        d: "fw_ldr_d"
    }
};
Object.keys(FW_CARDS).forEach(function(kind) {
    var c = FW_CARDS[kind],
        p = c.p,
        file = null,
        other = false;
    var info = $(p + "info"),
        btn = $(p + "up");
    $(p + "file").addEventListener("change", function() {
        var f = this.files[0];
        file = null;
        btn.disabled = true;
        info.textContent = "";
        if (!f) return;
        info.textContent = t("fw_checking", {
            f: f.name,
            n: f.size
        });
        f.arrayBuffer().then(function(buf) {
            var u = new Uint8Array(buf),
                r = fwClassify(u),
                msg = r.msg;
            if (!msg && r.kind !== kind) msg = t("fw_wrong_card", {
                c: t(FW_CARDS[r.kind].title)
            });
            if (msg) {
                info.innerHTML = '<span style="color:var(--bad)">✕ ' + esc(msg) + "</span>";
                return;
            }
            file = f;
            other = kind !== "mdata" && !fwForThisSwitch(u);
            info.innerHTML = '<span style="color:var(--ok)">✓ ' + esc(t(r.inst ? "fw_inst_valid" : c.valid)) + "</span>" +
                (other ? '<br><span style="color:var(--warn)">⚠ ' + esc(t("fw_other", {
                    m: fwBoardName()
                })) + "</span>" : "");
            btn.disabled = false;
        });
    });
    btn.addEventListener("click", function() {
        if (!file) return;
        var d = kind === "loader" && S.loader.present === "off" ? "ld_convert_d" : c.d;
        confirmModal(t("fw_q"), (other ? t("fw_other", {
            m: fwBoardName()
        }) + " " : "") + t(d), function() {
            fwSend(c, file, kind);
        });
    });
});

function fwSend(c, file, kind) {
    var form = new FormData();
    form.append("uploadedfile", file, file.name);
    var xhr = new XMLHttpRequest();
    var prog = $(c.p + "prog"),
        st = $(c.p + "stat"),
        btn = $(c.p + "up");
    var sent = false,
        settled = false,
        t0 = Date.now(),
        pct = 0;
    prog.style.display = "";
    prog.value = 0;
    Object.keys(FW_CARDS).forEach(function(k) {
        $(FW_CARDS[k].p + "up").disabled = true
    });

    function settle(fn) {
        if (settled) return;
        settled = true;
        clearInterval(tick);
        prog.style.display = "none";
        fn();
    }
    var tick = setInterval(function() {
        var s = Math.round((Date.now() - t0) / 1000);
        st.textContent = sent ? t("fw_finishing", {
            s: s
        }) : t("fw_uploading", {
            p: pct,
            s: s
        });
    }, 500);
    st.textContent = t("fw_uploading", {
        p: 0,
        s: 0
    });
    xhr.upload.onprogress = function(e) {
        if (e.lengthComputable) {
            pct = Math.round(100 * e.loaded / e.total);
            prog.value = pct;
        }
    };
    xhr.upload.onload = function() {
        sent = true;
        prog.removeAttribute("value");
    };
    xhr.onload = function() {
        settle(function() {
            if (xhr.status === 200 && kind === "mdata") {
                st.textContent = t("fw_md_done"); // in place, no reboot
                btn.disabled = false;
            } else if (xhr.status === 200) {
                st.textContent = t("fw_verified");
                fwSettle(st, true, kind === "loader", btn);
            } else {
                var why = (xhr.responseText || "").trim().split("\n")[0];
                st.textContent = "✕ " + t("fw_rejected") + " (HTTP " + xhr.status + (why ? ": " + why : "") + ")";
                btn.disabled = false;
            }
        })
    };
    xhr.onerror = function() {
        settle(function() {
            if (!sent) {
                st.textContent = t("fw_lost");
                btn.disabled = false;
                return;
            }
            fwSettle(st, false, false, btn);
        })
    };
    xhr.open("POST", c.ep);
    xhr.send(form);
}
/* knownGood: the firmware answered 200, so an early reply only means the
 * reset is still pending. Without a verdict an early reply means no reboot
 * happened, i.e. the image was rejected. Raw fetch: a 401 from the fresh
 * boot still counts as "the switch is back". */
function fwSettle(st, knownGood, loader, btn) {
    var waited = 3,
        down = false;

    function probe() {
        var ctl = ("AbortController" in window) ? new AbortController() : null;
        var to = setTimeout(function() {
            if (ctl) ctl.abort()
        }, 2500);
        fetch("/favicon.ico", {
            signal: ctl ? ctl.signal : undefined,
            cache: "no-store"
        }).then(function() {
            clearTimeout(to);
            if (!down && (loader || waited <= 9)) {
                if (!knownGood || waited > 30) {
                    st.textContent = "✕ " + t(loader ? "fw_ldr_failed" : "fw_noreboot");
                    btn.disabled = false;
                    return;
                }
                waited += 3;
                setTimeout(probe, 3000);
                return;
            }
            st.textContent = t("fw_applied") + " ✓";
            modal(t("fw_done_t"), h("p", {
                    text: t("fw_done")
                }),
                [h("button", {
                    class: "ctl pri",
                    text: t("fw_login"),
                    onclick: function() {
                        location.href = "/login.html"
                    }
                })]);
        }, function() {
            clearTimeout(to);
            down = true;
            waited += 3;
            st.textContent = t("fw_rebooting");
            if (waited > 150) {
                st.textContent = t("fw_timeout");
                btn.disabled = false;
                return;
            }
            setTimeout(probe, 3000);
        });
    }
    setTimeout(probe, 3000);
}

// What is on this switch, and which cards apply: the PoE card only on a board
// with a PoE controller (state.sys.poe not none) and room for its firmware.
function fwStatus() {
    var ld = S.loader.present,
        poe = S.sys.poe === "none" ? null : S.sys.poe,
        rows = [
            ["fw_s_ver", S.sys.version],
            ["fw_s_loader", ld == null ? null : t(ld === "on" ? "fw_s_yes" : "fw_s_no")],
            ["fw_s_poe", poe == null ? null : t({
                ok: "fw_poe_ok",
                no_image: "fw_poe_none",
                no_answer: "fw_poe_noanswer",
                no_chip_image: "fw_poe_nochip",
                no_room: "fw_poe_noroom"
            }[poe] || "fw_poe_failed")]
        ];
    var tb = $("fwstatus");
    tb.innerHTML = "";
    rows.forEach(function(r) {
        if (r[1] == null) return;
        tb.appendChild(h("tr", null, [h("td", {
            class: "mut",
            text: t(r[0])
        }), h("td", {
            class: "mono",
            text: String(r[1])
        })]));
    });
    $("pfcard").style.display = poe != null && poe !== "no_room" ? "" : "none";
    $("ldwarn").textContent = ld === "off" ? t("ld_none") : "";
}
tabHooks.fw = {
    enter: function() {
        stSysLoad().then(fwStatus).catch(function() {});
        fwStatus();
    }
};

/* The Schema page (DESIGN 13): help -a's lines (format 1: "help 1",
 * then path, type and form, default, unit, "tip", the tip the last
 * quoted, a default may be quoted too), a row each, objects bold,
 * filtered as typed. Another format gets a row saying so first; an
 * OM_HELP=0 build's err line, or "no reply", alone */
function schLoad() {
    var tb = $("scht").tBodies[0];
    if (tb.rows.length) return;
    cli(["help -a"]).then(function(r) {
        var o = r.out[0] || [],
            v = (/^help (\S+)$/.exec(o[0]) || [])[1];
        if (v) o = o.slice(1);
        if (v !== "1" && o.length) o.unshift("help format " + (v || "-") + ": this page reads help 1");
        (r.errs.length ? r.errs : o.length ? o : ["no reply"]).forEach(function(l) {
            var m = /^(\S+) (.*?) ?"([^"]*)"$/.exec(l) || [l, l, "", ""],
                tr = tb.insertRow();
            if (/^object/.test(m[2])) tr.style.fontWeight = "bold";
            for (var i = 1; i < 4; i++) tr.insertCell().textContent = m[i];
            tr.cells[1].style.cssText = "overflow-wrap:anywhere;width:40%";	/* a form of 78 words */
        });
    }).catch(function() {});
}
$("schf").addEventListener("input", function() {
    var f = this.value.toLowerCase();
    [].forEach.call($("scht").tBodies[0].rows, function(r) {
        r.hidden = r.textContent.toLowerCase().indexOf(f) < 0
    });
});
tabHooks.schema = {
    enter: schLoad
};

window.addEventListener("hashchange", function() {
    var id = location.hash.slice(1);
    if (TABS.some(function(tb) {
            return tb.id === id
        }) && id !== curTab) showTab(id);
});
(function() {
    var id = location.hash.slice(1);
    if (!TABS.some(function(tb) {
            return tb.id === id
        })) id = "dash";
    hostLoad().catch(function() {});
    showTab(id);
})();
