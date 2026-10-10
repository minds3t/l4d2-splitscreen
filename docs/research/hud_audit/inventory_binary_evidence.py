"""Capture the exact native PC/ISO inventory painter evidence; no game writes."""
from pathlib import Path
import hashlib
import json
import struct
import capstone
import pefile

ROOT = Path(__file__).resolve().parents[3]
PC = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Left 4 Dead 2\left4dead2\bin\client.dll")
XBOX = ROOT / "docs/research/xbox_lobby/client_360.dll.pe"
ENGINE = PC.parents[2] / "bin/engine.dll"
INPUT = PC.parents[2] / "bin/inputsystem.dll"

def capture(path, ranges, xbox=False):
    data = path.read_bytes()
    pe = pefile.PE(data=data, fast_load=True)
    image = data if xbox else pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    md = capstone.Cs(capstone.CS_ARCH_PPC if xbox else capstone.CS_ARCH_X86,
        capstone.CS_MODE_64 | capstone.CS_MODE_BIG_ENDIAN if xbox else capstone.CS_MODE_32)
    md.skipdata = True
    result = {"source": str(path), "sha256": hashlib.sha256(data).hexdigest(),
              "base": hex(base), "ranges": {}}
    for name, start, size in ranges:
        result["ranges"][name] = [f"{ins.address:08x}: {ins.bytes.hex()} {ins.mnemonic} {ins.op_str}"
            for ins in md.disasm(image[start-base:start-base+size], start)]
    if xbox:
        result["layout_constants"] = {
            "8810efb8_float": struct.unpack_from(">f", image, 0xefb8)[0],
            "88146d98_double": struct.unpack_from(">d", image, 0x46d98)[0],
        }
    return result

evidence = {
    "purpose": "Static ABI/layout evidence only; not runtime certification",
    "engine_binding": capture(ENGINE, [
        ("public_lookup_wrappers", 0x1005d2e0, 0x34),
        ("public_extended_lookup_wrapper", 0x1005e1e0, 0x1f),
        ("button_controller_slot", 0x101a2470, 0xcd),
        ("command_matcher_edi_abi", 0x101a27d0, 0xf7),
        ("filtered_lookup", 0x101a28d0, 0xde),
    ]),
    "input_binding": capture(INPUT, [
        ("native_button_name", 0x100010b0, 0x19),
        ("button_name_aliases", 0x10003c60, 0xca),
        ("xinput_record_initialize", 0x10005310, 0xfb),
        ("xinput_connect_disconnect", 0x10006250, 0xff),
    ]),
    "pc": capture(PC, [
        ("paint", 0x102d11e0, 0xd58),
        ("icon_draw", 0x1010cf20, 0xea),
        ("inactive_background", 0x102ce4c0, 0xe2),
        ("active_background", 0x102ce5b0, 0xe2),
        ("steam_input_item_glyph", 0x102ce980, 0x1a0),
        ("lastinv_binding_listener", 0x102ce2c0, 0x97),
        ("apply_scheme_icon_pointers", 0x102ce080, 0x1a7),
        ("panel_get_dimensions", 0x103e59c0, 0x60),
        ("surface_get_screen_size", 0x101b2a80, 0x1d),
        ("binding_y_accessor", 0x102cdac0, 0x1a),
    ]),
    "xbox": capture(XBOX, [
        ("paint", 0x8852aa98, 0xe10),
        ("button_to_item_slot", 0x88527930, 0x9c),
        ("apply_scheme_icon_pointers", 0x88526668, 0x1ac),
        ("screen_height_width", 0x882feed8, 0x58),
        ("panel_get_dimensions", 0x88638dd8, 0xd0),
    ], True),
}
(ROOT / "docs/research/hud_audit/inventory-binary-evidence.json").write_text(
    json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
print("Wrote docs/research/hud_audit/inventory-binary-evidence.json")
