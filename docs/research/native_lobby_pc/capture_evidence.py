"""Read-only, version-specific native lobby disassembly evidence.

Uses PE preferred addresses for decoding, but writes instruction locations as
module-relative RVAs so the notes do not depend on live ASLR addresses.
"""
from pathlib import Path
import hashlib
import json
import re

import capstone
import pefile


GAME = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Left 4 Dead 2")
OUT = Path(__file__).resolve().parent
SOURCES = {
    "gameui.dll": (GAME / "bin/gameui.dll", {
        "session_ready_window_route": (0x0DDA0, 0x0DE8D),
        "window_9_constructs_GameLobby": (0x0AE33, 0x0AE4F),
        "lobby_population": (0x4FA40, 0x4FBDD),
        "lobby_character_command": (0x4F904, 0x4F9BA),
        "lobby_selected_avatar_enumeration": (0x4DC90, 0x4DE7D),
        "lobby_character_menu_capacity_gate": (0x4FD91, 0x4FE4C),
        "lobby_offline_exit": (0x4B490, 0x4B511),
        "hybrid_mouse_pressed": (0x5ADD0, 0x5AE48),
        "base_button_mouse_released": (0xE6700, 0xE6840),
        "dropdown_command_and_opening_focus": (0x2D440, 0x2D504),
        "flyout_find_button_by_command": (0x2DA40, 0x2DAD0),
        "flyout_open_uses_initial_focus": (0x2E3A3, 0x2E441),
        "settings_back_closes_owned_session": (0x53650, 0x536C5),
        "settings_existing_lobby_classification": (0x543C0, 0x544EF),
        "settings_resource_selects_mode_edit": (0x55A40, 0x55CFB),
        "survivor_cast_aliases_and_labels": (0x66AA0, 0x66D30),
        "survivor_row_scheme_and_required_children": (0x4D2A0, 0x4D5DA),
        "survivor_row_native_name_position": (0x4C574, 0x4C671),
        "lobby_summary_label_lookup": (0x4F050, 0x4F25D),
        "lobby_access_summary_and_dropdown": (0x50010, 0x501B9),
        "hybrid_button_native_style_fonts": (0x5C680, 0x5C970),
        "lobby_resource_load_without_optional_conditions": (0x50D30, 0x50DB6),
        "lobby_chat_focus_test": (0x4EBD4, 0x4EC50),
        "lobby_chat_input_constructor": (0x51AF0, 0x51BFC),
        "base_panel_early_scheme_load": (0xD998, 0xD9BE),
        "base_panel_later_addon_reload": (0xD185, 0xD247),
        "ui_reloadscheme_active_resources_only": (0x16130, 0x161D0),
        "label_resource_insets_separate_fields": (0xE3CC0, 0xE3D50),
        "hybrid_scheme_inset_and_proportional_scale": (0x5CAE6, 0x5CB4F),
    }),
    "matchmaking.dll": (GAME / "left4dead2/bin/matchmaking.dll", {
        "offline_session_settings_access_and_update": (0x251E0, 0x25230),
        "offline_session_first_update_ready": (0x25230, 0x25281),
        "offline_endgame_and_disconnect_handler": (0x25290, 0x25377),
        "framework_run_frame": (0x1BFF0, 0x1C057),
        "keyvalues_copy_allocation_and_layout": (0x30FC0, 0x3104D),
        "keyvalues_copy_children_links": (0x3112F, 0x31161),
        "keyvalues_delta_envelope_and_update_merge": (0x31300, 0x314AD),
        "offline_session_initializer": (0x253E0, 0x255DD),
        "offline_session_constructor": (0x255E0, 0x25648),
        "offline_session_prepare_start": (0x25710, 0x25840),
        "offline_session_command": (0x25840, 0x25B68),
        "avatar_update_and_uniqueness": (0x06B80, 0x06DA0),
        "avatar_dispatch_branch": (0x06ED0, 0x06F8C),
        "local_slot_avatarinfo_to_server": (0x03B80, 0x03D5D),
    }),
    "engine.dll": (GAME / "bin/engine.dll", {
        "engine_client_factory_instance": (0x5CB80, 0x5CB86),
        "engine_client_ingame_and_connected": (0x5F1A0, 0x5F1C4),
        "escape_chat_gate": (0x21FD67, 0x21FE08),
    }),
    "client.dll": (GAME / "left4dead2/bin/client.dll", {
        "chat_active_registration": (0x4FFC90, 0x4FFCB2),
        "gameplay_chat_start_sets_active": (0x115B20, 0x115B5A),
        "gameplay_chat_stop_clears_active": (0x115E87, 0x115EEF),
        "gameplay_chat_reset_clears_active": (0x117C51, 0x117C67),
    }),
}


def main():
    result = {"captured": "2026-10-10", "address_format": "module-relative RVA", "modules": {}}
    for name, (path, ranges) in SOURCES.items():
        data = path.read_bytes()
        pe = pefile.PE(data=data, fast_load=True)
        base = pe.OPTIONAL_HEADER.ImageBase
        mapped = pe.get_memory_mapped_image()
        dis = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        evidence = {
            "path": str(path),
            "sha256": hashlib.sha256(data).hexdigest(),
            "file_size": len(data),
            "machine": hex(pe.FILE_HEADER.Machine),
            "pe_timestamp": pe.FILE_HEADER.TimeDateStamp,
            "ranges": {},
        }
        for label, (start, end) in ranges.items():
            lines = []
            for ins in dis.disasm(mapped[start:end], base + start):
                strings = []
                for match in re.finditer(r"0x([0-9a-f]{6,8})", ins.op_str):
                    rva = int(match[1], 16) - base
                    if 0 <= rva < len(mapped):
                        candidate = mapped[rva:rva + 160].split(b"\0")[0]
                        if len(candidate) >= 3 and all(32 <= c < 127 for c in candidate):
                            strings.append(candidate.decode())
                operands = re.sub(
                    r"0x([0-9a-f]{6,8})",
                    lambda m: f"{name}+0x{int(m[1], 16) - base:08x}"
                    if base <= int(m[1], 16) < base + len(mapped) else m[0],
                    ins.op_str,
                )
                lines.append({"rva": f"0x{ins.address - base:08x}",
                              "bytes": ins.bytes.hex(), "instruction": ins.mnemonic + " " + operands,
                              "strings": strings})
            evidence["ranges"][label] = {"start_rva": hex(start), "end_rva_exclusive": hex(end), "instructions": lines}
        result["modules"][name] = evidence
    (OUT / "disassembly.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("Wrote", OUT / "disassembly.json")


if __name__ == "__main__":
    main()
