#!/usr/bin/env python3
"""Generate CD image fixtures (BIN/CUE and, if chdman exists, CHD).

Usage: make_images.py OUTDIR

Creates under OUTDIR:
  simple/simple.{bin,cue}   data(300) + audio 440Hz with a synthetic PREGAP
  hidden/hidden.{bin,cue}   hidden 220Hz pregap audio in the BIN (INDEX 00)
  multi/{data,audio}.bin, multi.cue   one BIN per track
and .chd copies of simple and hidden when chdman is available.

Audio in BIN files is raw little-endian 16-bit stereo, 44100 Hz (CHD stores
it big-endian; chdman does the conversion).  The data track is a real
ISO 9660 image (genisoimage/xorriso) wrapped as MODE1/2352, so sector 16
holds the 'CD001' volume descriptor; without a tool it falls back to a
0xDA pattern.
"""
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile

SECTOR = 2352
SAMPLES = 588  # stereo sample pairs per sector
SYNC = b'\x00' + b'\xff' * 10 + b'\x00'


def bcd(v):
    return ((v // 10) << 4) | (v % 10)


def msf(lba):
    m, rem = divmod(lba + 150, 75 * 60)
    s, f = divmod(rem, 75)
    return bytes([bcd(m), bcd(s), bcd(f)])


def sine(freq, sectors, start=0):
    out = bytearray()
    for n in range(start * SAMPLES, (start + sectors) * SAMPLES):
        v = int(12000 * math.sin(2 * math.pi * freq * n / 44100))
        out += struct.pack('<hh', v, v)
    return bytes(out)


def silence(sectors):
    return bytes(sectors * SECTOR)


def mode1_sectors(user, count, first_lba=0):
    """Wrap 2048-byte user data blocks as MODE1/2352 (EDC/ECC left zero)."""
    out = bytearray()
    for i in range(count):
        blk = user[i * 2048:(i + 1) * 2048]
        blk = blk + b'\x00' * (2048 - len(blk))
        out += SYNC + msf(first_lba + i) + b'\x01' + blk + b'\x00' * 288
    return bytes(out)


def big_file():
    """20000 bytes (10 sectors) of deterministic, position dependent data."""
    return bytes((i * 7 + (i >> 8) * 13 + 1) & 0xff for i in range(20000))


def iso_image(workdir, sectors):
    for tool in ('genisoimage', 'mkisofs'):
        if shutil.which(tool):
            cmd = [tool]
            break
    else:
        if not shutil.which('xorriso'):
            return None
        cmd = ['xorriso', '-as', 'mkisofs']
    src = os.path.join(workdir, 'isosrc')
    os.makedirs(src, exist_ok=True)
    with open(os.path.join(src, 'HELLO.TXT'), 'w') as f:
        f.write('hello from the cd image test\n')
    with open(os.path.join(src, 'BIG.DAT'), 'wb') as f:
        f.write(big_file())
    iso = os.path.join(workdir, 'data.iso')
    subprocess.run(cmd + ['-quiet', '-V', 'CDIMGTEST', '-o', iso, src],
                   check=True, stderr=subprocess.DEVNULL)
    data = open(iso, 'rb').read()
    if len(data) > sectors * 2048:
        raise SystemExit('ISO larger than data track')
    return data


def data_track(workdir, sectors=300):
    iso = iso_image(workdir, sectors)
    if iso is None:
        user = b'\xda' * (2048 * sectors)
    else:
        user = iso.ljust(2048 * sectors, b'\x00')
    return mode1_sectors(user, sectors)


def write(path, data):
    with open(path, 'wb') as f:
        f.write(data)


def to_chd(cue, chd):
    if shutil.which('chdman'):
        r = subprocess.run(['chdman', 'createcd', '-f', '-i', cue, '-o', chd],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode:
            print('WARN: chdman failed on', cue)
            return False
        return True
    print('SKIP: chdman not found, no .chd for', cue)
    return False


def main(out):
    work = tempfile.mkdtemp()
    data = data_track(work)
    aud440 = sine(440, 375)

    d = os.path.join(out, 'simple')
    os.makedirs(d, exist_ok=True)
    write(os.path.join(d, 'simple.bin'), data + aud440)
    write(os.path.join(d, 'simple.cue'),
          b'FILE "simple.bin" BINARY\n'
          b'  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n'
          b'  TRACK 02 AUDIO\n    PREGAP 00:02:00\n    INDEX 01 00:04:00\n')
    to_chd(os.path.join(d, 'simple.cue'), os.path.join(d, 'simple.chd'))

    d = os.path.join(out, 'hidden')
    os.makedirs(d, exist_ok=True)
    pattern = mode1_sectors(b'\xda' * (2048 * 300), 300, 150)
    write(os.path.join(d, 'hidden.bin'), sine(220, 150) + pattern + aud440)
    write(os.path.join(d, 'hidden.cue'),
          b'FILE "hidden.bin" BINARY\n'
          b'  TRACK 01 AUDIO\n    INDEX 00 00:00:00\n    INDEX 01 00:02:00\n'
          b'  TRACK 02 AUDIO\n    INDEX 00 00:06:00\n    INDEX 01 00:06:00\n')
    to_chd(os.path.join(d, 'hidden.cue'), os.path.join(d, 'hidden.chd'))

    d = os.path.join(out, 'multi')
    os.makedirs(d, exist_ok=True)
    write(os.path.join(d, 'data.bin'), data)
    write(os.path.join(d, 'audio.bin'), silence(150) + aud440)
    write(os.path.join(d, 'multi.cue'),
          b'FILE "data.bin" BINARY\r\n'
          b'  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n'
          b'FILE "audio.bin" BINARY\r\n'
          b'  TRACK 02 AUDIO\r\n    INDEX 00 00:00:00\r\n'
          b'    INDEX 01 00:02:00\r\n')

    shutil.rmtree(work)
    print('images written to', out)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])
