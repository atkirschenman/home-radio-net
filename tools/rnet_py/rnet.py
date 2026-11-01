"""rnet reference implementation in Python.

Frame format, keys and MIC per docs/01-architecture.md sections 4-5. Used by
the generator for the golden test vectors, by host tools (sniffer,
provisioning) and as an independent check on lib/rnet: Python uses OpenSSL
(via `cryptography`), the C++ uses RadioLib; they share only the vectors.

    acronym  meaning
    rnet     home radio network (this protocol)
    mic      message integrity code: truncated AES-CMAC
    ack      acknowledgement
    rx       receive
"""
from __future__ import annotations

from dataclasses import dataclass

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.cmac import CMAC


def aes128_encrypt(key: bytes, block: bytes) -> bytes:
    encryptor = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
    return encryptor.update(block) + encryptor.finalize()


def cmac(key: bytes, message: bytes) -> bytes:
    authenticator = CMAC(algorithms.AES(key))
    authenticator.update(message)
    return authenticator.finalize()


def derive_node_key(master: bytes, node_id: int, node_type: int, label: bytes = b"rnet") -> bytes:
    block = node_id.to_bytes(2, "little") + bytes([node_type]) + label
    return aes128_encrypt(master, block + bytes(16 - len(block)))


CONTROL_DOWNLINK = 1 << 5
CONTROL_ACK_REQUEST = 1 << 4
CONTROL_RX_OPEN = 1 << 3
CONTROL_ACK = 1 << 2
CONTROL_MORE = 1 << 1
MIC_LENGTH = 4
HEADER_LENGTH = 10
ACK_COUNTER_LENGTH = 2


@dataclass
class Header:
    control: int
    net_id: int
    node_id: int
    node_type: int
    counter: int
    message_type: int
    ack_counter: int | None = None


def build_frame(key: bytes, header: Header, payload: bytes) -> bytes:
    control = header.control | (CONTROL_ACK if header.ack_counter is not None else 0)
    frame = bytes([control, header.net_id]) + header.node_id.to_bytes(2, "little") + bytes([header.node_type])
    frame += header.counter.to_bytes(4, "little") + bytes([header.message_type])
    if header.ack_counter is not None:
        frame += (header.ack_counter & 0xFFFF).to_bytes(ACK_COUNTER_LENGTH, "little")
    frame += payload
    return frame + cmac(key, frame)[:MIC_LENGTH]


def parse_frame(frame: bytes) -> tuple[Header, bytes]:
    """Header and payload, without checking the message integrity code (verify_frame does)."""
    if len(frame) < HEADER_LENGTH + MIC_LENGTH:
        raise ValueError("frame too short")
    header = Header(
        control=frame[0],
        net_id=frame[1],
        node_id=int.from_bytes(frame[2:4], "little"),
        node_type=frame[4],
        counter=int.from_bytes(frame[5:9], "little"),
        message_type=frame[9],
    )
    payload_start = HEADER_LENGTH
    if header.control & CONTROL_ACK:
        if len(frame) < HEADER_LENGTH + ACK_COUNTER_LENGTH + MIC_LENGTH:
            raise ValueError("frame too short for ack_counter")
        header.ack_counter = int.from_bytes(frame[payload_start : payload_start + ACK_COUNTER_LENGTH], "little")
        payload_start += ACK_COUNTER_LENGTH
    return header, frame[payload_start:-MIC_LENGTH]


def verify_frame(key: bytes, frame: bytes) -> bool:
    return len(frame) > MIC_LENGTH and cmac(key, frame[:-MIC_LENGTH])[:MIC_LENGTH] == frame[-MIC_LENGTH:]
