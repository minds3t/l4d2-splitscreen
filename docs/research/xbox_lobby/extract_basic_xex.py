"""Extract this disc's basic-compressed XEX2 payloads for static analysis.

Format reference: https://github.com/xenia-project/xenia/blob/master/src/xenia/cpu/xex_module.cc
This does not modify, rebuild or execute a console executable.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "vendor"))
from Crypto.Cipher import AES
import pefile

RETAIL_KEY = bytes.fromhex("20b185a59d28fdc340583fbb0896bf91")


def extract(path):
    data = path.read_bytes()
    magic, flags, payload_offset, _, security_offset, count = struct.unpack_from(">6I", data)
    assert magic == 0x58455832
    options = dict(struct.iter_unpack(">II", data[24:24 + 8 * count]))
    format_offset = options[0x3ff]
    size, encryption, compression = struct.unpack_from(">IHH", data, format_offset)
    assert compression == 1, "This narrow extractor supports basic compression only"
    blocks = list(struct.iter_unpack(">II", data[format_offset + 8:format_offset + size]))
    total_encoded = sum(encoded for encoded, zeroes in blocks)
    encoded = data[payload_offset:payload_offset + total_encoded]
    assert len(encoded) == total_encoded
    if encryption == 1:
        session = AES.new(RETAIL_KEY, AES.MODE_CBC, bytes(16)).decrypt(data[security_offset + 0x150:security_offset + 0x160])
        encoded = AES.new(session, AES.MODE_CBC, bytes(16)).decrypt(encoded)
    else:
        assert encryption == 0
    result, offset = bytearray(), 0
    for size, zeroes in blocks:
        result.extend(encoded[offset:offset + size])
        result.extend(bytes(zeroes))
        offset += size
    assert result[:2] == b"MZ", "Invalid decoded DOS header"
    pe = pefile.PE(data=bytes(result), fast_load=True)
    assert pe.FILE_HEADER.Machine == 0x1f2, hex(pe.FILE_HEADER.Machine)
    output = HERE / (path.name + ".pe")
    output.write_bytes(result)
    metadata = {
        "source": str(path), "source_sha256": hashlib.sha256(data).hexdigest(),
        "output": str(output), "output_sha256": hashlib.sha256(result).hexdigest(),
        "output_bytes": len(result), "encryption": encryption, "compression": compression,
        "image_base": hex(pe.OPTIONAL_HEADER.ImageBase),
        "entry_point": hex(pe.OPTIONAL_HEADER.ImageBase + pe.OPTIONAL_HEADER.AddressOfEntryPoint),
        "sections": [{"name": s.Name.rstrip(b"\0").decode(), "va": hex(s.VirtualAddress),
                      "size": s.Misc_VirtualSize, "raw": hex(s.PointerToRawData)} for s in pe.sections],
    }
    output.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    for name in sys.argv[1:]:
        extract(Path(name))
