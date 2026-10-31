"""rnet reference implementation in Python.

Frame format, keys and MIC per docs/01-architecture.md sections 4-5. Used by
the generator for the golden test vectors, by host tools (sniffer,
provisioning) and as an independent check on lib/rnet's C: Python uses
OpenSSL (via `cryptography`), the C uses TinyCrypt; they share only the
vectors.
"""
from __future__ import annotations

from dataclasses import dataclass

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.cmac import CMAC


# AES and CMAC come from the `cryptography` package (OpenSSL), not our code.
def aes128_encrypt(key: bytes, block: bytes) -> bytes:
    enc = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
    return enc.update(block) + enc.finalize()


def cmac(key: bytes, msg: bytes) -> bytes:
    c = CMAC(algorithms.AES(key))
    c.update(msg)
    return c.finalize()


# ---------------------------------------------------------------------------
# Keys (section 5.1)
# ---------------------------------------------------------------------------
def derive_node_key(master: bytes, node_id: int, node_type: int, label: bytes = b"rnet") -> bytes:
    block = node_id.to_bytes(2, "little") + bytes([node_type]) + label
    return aes128_encrypt(master, block + bytes(16 - len(block)))


# ---------------------------------------------------------------------------
# Frame (section 4)
# ---------------------------------------------------------------------------
CTRL_DIR = 1 << 5
CTRL_ACK_REQ = 1 << 4
CTRL_RX_OPEN = 1 << 3
CTRL_ACK = 1 << 2
CTRL_MORE = 1 << 1
MIC_LEN = 4
HEADER_LEN = 10


@dataclass
class Header:
    ctrl: int
    net_id: int
    node_id: int
    node_type: int
    ctr: int
    msg_type: int
    ack_ctr: int | None = None  # present iff ctrl has CTRL_ACK


def build_frame(key: bytes, h: Header, payload: bytes) -> bytes:
    ctrl = h.ctrl | (CTRL_ACK if h.ack_ctr is not None else 0)
    out = bytes([ctrl, h.net_id]) + h.node_id.to_bytes(2, "little") + bytes([h.node_type])
    out += h.ctr.to_bytes(4, "little") + bytes([h.msg_type])
    if h.ack_ctr is not None:
        out += (h.ack_ctr & 0xFFFF).to_bytes(2, "little")
    out += payload
    return out + cmac(key, out)[:MIC_LEN]


def parse_frame(frame: bytes) -> tuple[Header, bytes]:
    """Header and payload, without checking the MIC (verify_frame does)."""
    if len(frame) < HEADER_LEN + MIC_LEN:
        raise ValueError("frame too short")
    ctrl = frame[0]
    h = Header(
        ctrl=ctrl,
        net_id=frame[1],
        node_id=int.from_bytes(frame[2:4], "little"),
        node_type=frame[4],
        ctr=int.from_bytes(frame[5:9], "little"),
        msg_type=frame[9],
    )
    off = HEADER_LEN
    if ctrl & CTRL_ACK:
        if len(frame) < HEADER_LEN + 2 + MIC_LEN:
            raise ValueError("frame too short for ack_ctr")
        h.ack_ctr = int.from_bytes(frame[off : off + 2], "little")
        off += 2
    return h, frame[off:-MIC_LEN]


def verify_frame(key: bytes, frame: bytes) -> bool:
    return len(frame) > MIC_LEN and cmac(key, frame[:-MIC_LEN])[:MIC_LEN] == frame[-MIC_LEN:]
