"""Read-only PC/ISO evidence for CBindPanel's per-player prompt lookup."""
from pathlib import Path
import hashlib
import json
import capstone
import pefile

ROOT = Path(__file__).resolve().parents[3]
GAME = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Left 4 Dead 2")


def capture(path, ranges, xbox=False):
    data = path.read_bytes()
    pe = pefile.PE(data=data, fast_load=True)
    image = data if xbox else pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    md = capstone.Cs(capstone.CS_ARCH_PPC if xbox else capstone.CS_ARCH_X86,
        capstone.CS_MODE_64 | capstone.CS_MODE_BIG_ENDIAN if xbox else capstone.CS_MODE_32)
    md.skipdata = True
    return {"source": str(path), "sha256": hashlib.sha256(data).hexdigest(),
            "base": hex(base), "ranges": {
                name: [f"{i.address:08x}: {i.bytes.hex()} {i.mnemonic} {i.op_str}"
                       for i in md.disasm(image[va-base:va-base+size], va)]
                for name, va, size in ranges}}


evidence = {
    "scope": "Static binding lookup and native renderer evidence; not runtime certification.",
    "pc": capture(GAME / "left4dead2/bin/client.dll", [
        ("takeover_bind_attack", 0x102e5fea, 0x36),
        ("bindpanel_lookup_and_native_refresh", 0x102d687f, 0x153),
        ("bindpanel_periodic_refresh", 0x102d6a00, 0x19),
        ("bindpanel_resource_slot", 0x102d6a70, 0xc8),
        ("bindpanel_console_recognition", 0x102d5c60, 0x212),
        ("bindpanel_native_glyph_render", 0x102d63cf, 0x1e9),
        ("joystick_registration", 0x10501640, 0x26),
        ("vote_start_local_slot_context", 0x102db130, 0x5b),
        ("vote_start_pc_labels", 0x102db589, 0x9d),
        ("vote_start_return", 0x102db7cc, 0x1c),
        ("vote_constructor_vtables", 0x102dc310, 0x32),
        ("hud_local_slot_accessor", 0x1010cda0, 0x7),
        ("control_string_lookup_only", 0x103ff650, 0x58),
        ("panel_visibility", 0x103e4740, 0x24),
    ]),
    "engine": capture(GAME / "bin/engine.dll", [
        ("extended_lookup_thiscall", 0x1005e1e0, 0x1f),
        ("lookup_ignores_user_but_filters_device", 0x101a28d0, 0xde),
        ("button_controller_slot", 0x101a2470, 0xcd),
        ("command_matcher_edi", 0x101a27d0, 0xf7),
    ]),
    "xbox": capture(ROOT / "docs/research/xbox_lobby/client_360.dll.pe", [
        ("bindpanel_update", 0x88530af8, 0x12c),
        ("bindpanel_console_recognition", 0x885309c0, 0x138),
        ("bindpanel_native_glyph_render", 0x88530c28, 0x270),
    ], True),
}
path = ROOT / "docs/research/hud_audit/prompt-binary-evidence.json"
path.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
print(path.relative_to(ROOT))
