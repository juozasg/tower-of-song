#!/usr/bin/env python3
"""Writes a few public-domain melodies as MIDI files into assets/catalog.

Your own catalog goes in the same folder (or the user data folder's catalog/);
the game picks each song's melody track and cuts it into bar-aligned phrases.
"""

import pathlib
import struct

TPQ = 480

# (pitch, beats); pitch 0 is a rest.
SONGS = {
    "ode_to_joy": (120, [
        (64, 1), (64, 1), (65, 1), (67, 1), (67, 1), (65, 1), (64, 1), (62, 1),
        (60, 1), (60, 1), (62, 1), (64, 1), (64, 1.5), (62, 0.5), (62, 2),
        (64, 1), (64, 1), (65, 1), (67, 1), (67, 1), (65, 1), (64, 1), (62, 1),
        (60, 1), (60, 1), (62, 1), (64, 1), (62, 1.5), (60, 0.5), (60, 2),
    ]),
    "twinkle_twinkle": (100, [
        (60, 1), (60, 1), (67, 1), (67, 1), (69, 1), (69, 1), (67, 2),
        (65, 1), (65, 1), (64, 1), (64, 1), (62, 1), (62, 1), (60, 2),
        (67, 1), (67, 1), (65, 1), (65, 1), (64, 1), (64, 1), (62, 2),
        (67, 1), (67, 1), (65, 1), (65, 1), (64, 1), (64, 1), (62, 2),
    ]),
    "frere_jacques": (110, [
        (60, 1), (62, 1), (64, 1), (60, 1), (60, 1), (62, 1), (64, 1), (60, 1),
        (64, 1), (65, 1), (67, 2), (64, 1), (65, 1), (67, 2),
        (67, 0.5), (69, 0.5), (67, 0.5), (65, 0.5), (64, 1), (60, 1),
        (67, 0.5), (69, 0.5), (67, 0.5), (65, 0.5), (64, 1), (60, 1),
        (60, 1), (55, 1), (60, 2), (60, 1), (55, 1), (60, 2),
    ]),
    "when_the_saints": (120, [
        (0, 1), (60, 1), (64, 1), (65, 1), (67, 4),
        (0, 1), (60, 1), (64, 1), (65, 1), (67, 4),
        (0, 1), (60, 1), (64, 1), (65, 1), (67, 2), (64, 2), (60, 2), (64, 2), (62, 4),
    ]),
}


def var_len(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.insert(0, (value & 0x7F) | 0x80)
        value >>= 7
    return bytes(out)


def song_to_midi(bpm, notes):
    track = bytearray()
    track += b"\x00\xff\x51\x03" + struct.pack(">I", int(60_000_000 / bpm))[1:]
    track += b"\x00\xff\x58\x04\x04\x02\x18\x08"
    pending_rest = 0
    for pitch, beats in notes:
        ticks = int(beats * TPQ)
        if pitch == 0:
            pending_rest += ticks
            continue
        sounding = int(ticks * 0.92)
        track += var_len(pending_rest) + bytes([0x90, pitch, 96])
        track += var_len(sounding) + bytes([0x80, pitch, 0])
        pending_rest = ticks - sounding
    track += var_len(pending_rest) + b"\xff\x2f\x00"
    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, TPQ)
    return header + b"MTrk" + struct.pack(">I", len(track)) + bytes(track)


def main():
    out_dir = pathlib.Path(__file__).resolve().parent.parent / "assets" / "catalog"
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, (bpm, notes) in SONGS.items():
        (out_dir / f"{name}.mid").write_bytes(song_to_midi(bpm, notes))
        print("wrote", out_dir / f"{name}.mid")


if __name__ == "__main__":
    main()
