"""Capture read-only ISO aim-assist evidence and compare current PC scripts.

Run from the repository root with Python, pefile, capstone and the existing
VPK build dependencies installed. If needed, first extract server_360.dll:
python docs/research/xbox_lobby/extract_basic_xex.py xbox_raw/left4dead2/bin/server_360.dll
"""
from pathlib import Path
import hashlib
import json
import re
import struct
import sys

import capstone
import pefile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "packaging"))
from game_files import read_stock


def capture(name, ranges):
    path = ROOT / "docs/research/xbox_lobby" / (name + "_360.dll.pe")
    data = path.read_bytes()
    pe = pefile.PE(data=data, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    md = capstone.Cs(capstone.CS_ARCH_PPC,
                     capstone.CS_MODE_64 | capstone.CS_MODE_BIG_ENDIAN)
    md.skipdata = True
    # The extractor expands the XEX basic-compression blocks to memory layout;
    # its embedded PE raw section offsets must not be applied a second time.
    return {"source": str(path.relative_to(ROOT)),
            "sha256": hashlib.sha256(data).hexdigest(), "base": hex(base),
            "ranges": {label: [f"{i.address:08x}: {i.bytes.hex()} {i.mnemonic} {i.op_str}"
                               for i in md.disasm(data[va-base:va-base+size], va)]
                       for label, va, size in ranges}}


def settings(path):
    data = path.read_bytes()
    text = data.decode("utf-8-sig")
    return {"source": str(path.relative_to(ROOT)),
            "sha256": hashlib.sha256(data).hexdigest(),
            "lines": {str(n): line for n, line in enumerate(text.splitlines(), 1)
                      if re.search(r"autoaim|mouselook|unbindall|controller2|joy_|cl_yawspeed|cl_pitchspeed|zoom_sensitivity", line, re.I)}}


weapon_fields = re.compile(
    r'"(MaxAutoAimDeflection1|MaxAutoAimRange1|WeaponAutoAimScale)"\s*"([^"]+)"')
weapons = {}
for path in sorted((ROOT / "xbox_ui/left4dead2/scripts").glob("weapon_*.txt")):
    xbox = path.read_bytes()
    values = dict(weapon_fields.findall(xbox.decode("utf-8-sig")))
    if not values:
        continue
    pc = read_stock("scripts/" + path.name)
    pc_text = pc.decode("utf-8-sig") if isinstance(pc, bytes) else pc
    pc_bytes = pc if isinstance(pc, bytes) else pc.encode("utf-8")
    weapons[path.name] = {"xbox": values, "pc": dict(weapon_fields.findall(pc_text)),
                          "xbox_sha256": hashlib.sha256(xbox).hexdigest(),
                          "pc_sha256": hashlib.sha256(pc_bytes).hexdigest()}

result = {
    "scope": "Static primary-binary/config evidence; no live aim-assist certification.",
    "client": capture("client", [
        ("native_slot_aware_local_player", 0x882a56e0, 0x6c),
        ("native_stick_damping_factor", 0x88383a50, 0xa0),
        ("damping_factor_applied_to_stick_curve", 0x88383cb0, 0x7c),
        ("damping_cvar_registration", 0x886dad50, 0xa0),
        ("sk_autoaim_mode_registration", 0x886d82c0, 0x48),
        ("receive_target_field", 0x882a78ac, 0x18),
        ("mode1_radial_pegged_response", 0x88383d50, 0x230),
        ("input_timestep_per_slot_accumulator", 0x88384378, 0x118),
        ("raw_input_axis_normalization", 0x8838471c, 0x98),
        ("variable_frametime_selection", 0x88384998, 0x54),
        ("yaw_response_and_final_rate", 0x88384c2c, 0xac),
        ("pitch_response_and_final_rate", 0x88384dd8, 0x68),
        ("variable_frametime_registration", 0x886da618, 0x48),
        ("pegged_and_filter_registration", 0x886db350, 0xd8),
        ("yaw_and_pitch_speed_registration", 0x886db470, 0x90),
    ]),
    "inputsystem": capture("inputsystem", [
        ("xinput_axis_default_deadzones_and_scales", 0x83512678, 0xfc),
        ("signed_axis_to_analog_index_and_publication", 0x83512778, 0x1bc),
        ("left_stick_deadzone_and_rescaling", 0x83512c20, 0x1bc),
        ("right_stick_deadzone_and_rescaling", 0x83512ddc, 0x1c4),
        ("native_xinput_poll_and_processing", 0x835130b0, 0x140),
        ("native_xinput_call_adapter", 0x83513798, 0xc),
    ]),
    "server": capture("server", [
        ("native_survivor_nonbot_eligibility", 0x89a6d540, 0x70),
        ("native_team_number", 0x89942eb0, 0x8),
        ("native_fakeclient_flag", 0x89a73210, 0xc),
        ("per_client_damping_query_and_target_publication", 0x89a7ab18, 0x1ac),
        ("target_vector_eligibility_and_dispatch", 0x89a835a0, 0xac),
        ("target_result_and_direct_only_early_exit", 0x89a7d660, 0x54),
        ("sk_autoaim_mode_registration", 0x89e17090, 0x48),
        ("autoaim_distance_deflection_registration", 0x89e1c480, 0x90),
        ("send_target_field", 0x89a90da0, 0x18),
    ]),
    "configs": [settings(ROOT / "xbox_ui/left4dead2/cfg" / name)
                for name in ("config.360.cfg", "360controller.cfg", "undo360controller.cfg")],
    "weapon_fields": weapons,
}
input_data = (ROOT / "docs/research/xbox_lobby/inputsystem_360.dll.pe").read_bytes()
result["inputsystem"]["axis_scale_constants"] = {
    hex(0x83500000 + offset): struct.unpack_from(">f", input_data, offset)[0]
    for offset in (0xc78, 0xc7c, 0xc80, 0xc84)
}
output = Path(__file__).with_name("xbox-evidence.json")
output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
print(output.relative_to(ROOT))
print(f"Compared {len(weapons)} weapon scripts; identical aim fields:",
      all(v["xbox"] == v["pc"] for v in weapons.values()))
