import re
import shutil
import subprocess
import sys
from pathlib import Path
from unittest import SkipTest

from common_framework import BINSDIR


def _make_images(self):
    """Generate the BIN/CUE fixtures once per test into the test's dir."""
    if not (shutil.which("genisoimage") or shutil.which("mkisofs")
            or shutil.which("xorriso")):
        raise SkipTest("needs genisoimage/mkisofs/xorriso for a data track")
    out = Path(self.imagedir) / "cdimg"
    gen = Path(self.topdir) / "test" / "cdimg" / "make_images.py"
    r = subprocess.run([sys.executable, "-I", str(gen), str(out)],
                       capture_output=True, text=True)
    if r.returncode:
        raise SkipTest("cannot build CD images: " + r.stderr[-200:])
    return out


def mscdex_image(self):
    # MSCDEX sector reads from a BIN/CUE-backed CD-ROM unit: the ISO 9660
    # volume descriptor in the data track must come back through
    # INT 2Fh AX=1508h, and a read of an audio track or past the end of the
    # disc must fail instead of returning bytes.
    out = _make_images(self)
    hostdir = Path(self.imagedir) / "cdfiles"
    hostdir.mkdir(exist_ok=True)

    config = """\
$_hdimage = "dXXXXs/c:hdtype1 +1"
$_floppy_a = ""
$_cdrom = "%s"
$_hostfs_drives = "%s:c"
""" % (out / "simple" / "simple.cue", hostdir)

    self.mkfile("testit.bat", """\
c:\\cdimg
rem end
""", newline="\r\n")

    self.mkcom_with_nasm("cdimg", r"""

bits 16
cpu 386

org 100h

section .text

    push    cs
    pop     ds
    push    cs
    pop     es

    ; AX=1500h - installation check, BX = drive count, CX = first drive
    mov     ax, 1500h
    xor     bx, bx
    xor     cx, cx
    int     2fh
    mov     [ndrv], bx
    mov     [fdrv], cx

    ; AX=1508h - read 1 sector at HSG sector 16 (the volume descriptor)
    mov     ax, 1508h
    mov     cx, [fdrv]
    mov     bx, buf
    xor     si, si
    mov     di, 16
    mov     dx, 1
    clc
    int     2fh
    call    getcf
    mov     [cf1], ax

    ; sector 400 is in the audio track: must fail
    mov     ax, 1508h
    mov     cx, [fdrv]
    mov     bx, buf2
    xor     si, si
    mov     di, 400
    mov     dx, 1
    clc
    int     2fh
    call    getcf
    mov     [cf2], ax

    ; far past the end of the disc: must fail
    mov     ax, 1508h
    mov     cx, [fdrv]
    mov     bx, buf2
    mov     si, 1
    mov     di, 0
    mov     dx, 1
    clc
    int     2fh
    call    getcf
    mov     [cf3], ax

    mov     ax, [ndrv]
    mov     di, outn
    call    hex16
    mov     ax, [cf1]
    mov     di, outa
    call    hex16
    mov     ax, [cf2]
    mov     di, outb
    call    hex16
    mov     ax, [cf3]
    mov     di, outc
    call    hex16

    mov     si, buf+1
    mov     di, outid
    mov     cx, 5
    rep movsb

    mov     ah, 9
    mov     dx, result
    int     21h

    mov     ax, 4c00h
    int     21h

; ax = 1 if the carry flag is set, else 0
getcf:
    mov     ax, 0
    jnc     .done
    mov     ax, 1
.done:
    ret

; ax = value to print, di -> 4 chars of output
hex16:
    mov     cx, 4
.loop:
    rol     ax, 4
    push    ax
    and     al, 0fh
    add     al, '0'
    cmp     al, '9'
    jbe     .store
    add     al, 7
.store:
    mov     [di], al
    inc     di
    pop     ax
    loop    .loop
    ret

section .data

ndrv:   dw 0
fdrv:   dw 0
cf1:    dw 0
cf2:    dw 0
cf3:    dw 0

result:
    db "NDRV("
outn:
    db "XXXX"
    db ") READ("
outa:
    db "XXXX"
    db ") AUDIO("
outb:
    db "XXXX"
    db ") PAST("
outc:
    db "XXXX"
    db ") ID("
outid:
    db "XXXXX"
    db ")",13,10,'$'

section .bss
buf:    resb 2048
buf2:   resb 2048
""")

    results = self.runDosemu("testit.bat", config=config)

    r = re.compile(r'NDRV\(([0-9A-F]{4})\) READ\(([0-9A-F]{4})\) '
                   r'AUDIO\(([0-9A-F]{4})\) PAST\(([0-9A-F]{4})\) '
                   r'ID\((.{5})\)')
    self.assertRegex(results, r)
    t = r.search(results)
    self.assertGreaterEqual(int(t.group(1), 16), 1, results)
    self.assertEqual(int(t.group(2), 16), 0, results)
    self.assertEqual(t.group(5), "CD001", results)
    self.assertEqual(int(t.group(3), 16), 1, results)
    self.assertEqual(int(t.group(4), 16), 1, results)


def mscdex_image_toc(self):
    # Audio disc info and track info as the DOS driver gets them from the
    # helper (INT E6h, AL=40h, AH=command, DS:SI -> request data).  The
    # simple disc is a data track at 00:02:00 and an audio track whose
    # INDEX 01 is at LBA 450, 00:08:00 on the disc, lead-out at 00:13:00.
    out = _make_images(self)
    hostdir = Path(self.imagedir) / "cdfiles"
    hostdir.mkdir(exist_ok=True)

    config = """\
$_hdimage = "dXXXXs/c:hdtype1 +1"
$_floppy_a = ""
$_cdrom = "%s"
$_hostfs_drives = "%s:c"
""" % (out / "simple" / "simple.cue", hostdir)

    self.mkfile("testit.bat", """\
c:\\cdtoc
rem end
""", newline="\r\n")

    self.mkcom_with_nasm("cdtoc", r"""

bits 16
cpu 386

org 100h

section .text

    push    cs
    pop     ds
    push    cs
    pop     es

    ; audio disc info
    mov     di, req
    mov     al, 0
    mov     cx, 8
    rep stosb
    mov     si, req
    mov     al, 40h
    mov     ah, 10h
    int     0e6h
    mov     si, req
    mov     di, outd
    call    hex8s

    ; track 2 info
    mov     di, req
    mov     al, 0
    mov     cx, 8
    rep stosb
    mov     byte [req+1], 2
    mov     si, req
    mov     al, 40h
    mov     ah, 11h
    int     0e6h
    mov     si, req
    mov     di, outt2
    call    hex8s

    ; track 1 info
    mov     di, req
    mov     al, 0
    mov     cx, 8
    rep stosb
    mov     byte [req+1], 1
    mov     si, req
    mov     al, 40h
    mov     ah, 11h
    int     0e6h
    mov     si, req
    mov     di, outt1
    call    hex8s

    ; track 3 does not exist: AL must come back nonzero
    mov     byte [req+1], 3
    mov     si, req
    mov     al, 40h
    mov     ah, 11h
    int     0e6h
    mov     [ax3], ax
    mov     ax, [ax3]
    mov     di, out3
    call    hex16

    mov     ah, 9
    mov     dx, result
    int     21h

    mov     ax, 4c00h
    int     21h

; si -> 8 bytes, di -> 16 chars of output
hex8s:
    mov     cx, 8
.next:
    lodsb
    push    cx
    mov     ah, al
    shr     al, 4
    call    nib
    mov     al, ah
    and     al, 0fh
    call    nib
    pop     cx
    loop    .next
    ret
nib:
    add     al, '0'
    cmp     al, '9'
    jbe     .st
    add     al, 7
.st:
    mov     [di], al
    inc     di
    ret

hex16:
    mov     cx, 4
.loop:
    rol     ax, 4
    push    ax
    and     al, 0fh
    call    nib
    pop     ax
    loop    .loop
    ret

section .data

ax3:    dw 0

result:
    db "D("
outd:
    db "XXXXXXXXXXXXXXXX"
    db ") T2("
outt2:
    db "XXXXXXXXXXXXXXXX"
    db ") T1("
outt1:
    db "XXXXXXXXXXXXXXXX"
    db ") T3("
out3:
    db "XXXX"
    db ")",13,10,'$'

section .bss
req:    resb 16
""")

    results = self.runDosemu("testit.bat", config=config)

    r = re.compile(r'D\(([0-9A-F]{16})\) T2\(([0-9A-F]{16})\) '
                   r'T1\(([0-9A-F]{16})\) T3\(([0-9A-F]{4})\)')
    self.assertRegex(results, r)
    t = r.search(results)
    # disc info: [0] untouched, lowest 1, highest 2, lead-out frame,
    # second, minute, 0 -> 00:13:00
    self.assertEqual(t.group(1), "000102000D000000", results)
    # track 2: frame, second, minute, 0 at +2..5, control at +6
    self.assertEqual(t.group(2)[:2], "00", results)
    self.assertEqual(t.group(2)[4:12], "00080000", results)
    self.assertEqual(t.group(2)[12:14], "20", results)
    # track 1 is data: 00:02:00, control 0x60
    self.assertEqual(t.group(3)[4:12], "00020000", results)
    self.assertEqual(t.group(3)[12:14], "60", results)
    self.assertNotEqual(int(t.group(4), 16) & 0xff, 0, results)


def _run_audio(self, extra=""):
    # Play on the hidden-track disc through the helper: the head must start
    # in the pregap (index 0, relative time counting down), cross INDEX 01
    # on the clock, freeze while paused, and stop by itself at the end of
    # the range.  Works without sound; with it the PCM goes to the mixer.
    out = _make_images(self)
    hostdir = Path(self.imagedir) / "cdfiles"
    hostdir.mkdir(exist_ok=True)

    config = """\
$_hdimage = "dXXXXs/c:hdtype1 +1"
$_floppy_a = ""
$_cdrom = "%s"
$_hostfs_drives = "%s:c"
""" % (out / "hidden" / "hidden.cue", hostdir) + extra

    self.mkfile("testit.bat", """\
c:\\cdaud
rem end
""", newline="\r\n")

    self.mkcom_with_nasm("cdaud", r"""

bits 16
cpu 386

org 100h

section .text

    push    cs
    pop     ds
    push    cs
    pop     es

    ; play HSG sector 0, 450 sectors (6 s), starting inside the pregap
    mov     dx, 450
    xor     bx, bx
    call    play
    mov     di, o_play
    call    emit_ax

    mov     di, o_q1
    call    qchan

    mov     bx, 56              ; ~3.1 s
    call    wait_ticks
    mov     di, o_q2
    call    qchan

    ; pause, then the head must not move
    mov     ah, 05h
    call    helper
    mov     di, o_pause
    call    emit_ax
    mov     di, o_st1
    call    astatus
    mov     di, o_q3
    call    qchan
    mov     bx, 6
    call    wait_ticks
    mov     di, o_q4
    call    qchan

    mov     ah, 06h
    call    helper
    mov     di, o_resume
    call    emit_ax

    ; stop: the first 05h paused, so this one stops it
    mov     ah, 05h
    call    helper
    mov     ah, 05h
    call    helper
    mov     di, o_st2
    call    astatus

    ; a short range stops by itself
    mov     dx, 38
    xor     bx, bx
    call    play
    mov     bx, 18
    call    wait_ticks
    mov     di, o_st3
    call    astatus

    mov     ah, 9
    mov     dx, result
    int     21h

    mov     ax, 4c00h
    int     21h

; call the cdrom helper, AH = command, DS:SI -> buf, ES:DI -> req
helper:
    mov     si, buf
    mov     di, req
    mov     al, 40h
    int     0e6h
    mov     [lastax], ax
    ret

; play: bx = start sector, dx = count
play:
    push    di
    mov     di, req
    xor     ax, ax
    mov     cx, 16
    rep stosw
    mov     byte [req+13], 0
    mov     [req+14], bx
    mov     [req+18], dx
    pop     di
    mov     ah, 04h
    jmp     helper

; Q-channel into the output at di: ax then the 11 byte reply
qchan:
    push    di
    mov     di, buf
    xor     ax, ax
    mov     cx, 8
    rep stosw
    mov     ah, 13h
    call    helper
    pop     di
    jmp     emit_all

; audio status into the output at di
astatus:
    push    di
    mov     di, buf
    xor     ax, ax
    mov     cx, 8
    rep stosw
    mov     ah, 14h
    call    helper
    pop     di
    jmp     emit_all

emit_all:
    call    emit_ax
    mov     si, buf
    mov     cx, 11
.next:
    lodsb
    call    hexbyte
    loop    .next
    ret

emit_ax:
    mov     ax, [lastax]
    xchg    al, ah
    call    hexbyte
    xchg    al, ah
    call    hexbyte
    ret

hexbyte:
    push    ax
    shr     al, 4
    call    nib
    pop     ax
    and     al, 0fh
nib:
    add     al, '0'
    cmp     al, '9'
    jbe     .st
    add     al, 7
.st:
    mov     [di], al
    inc     di
    ret

; bx = number of BIOS ticks to wait
wait_ticks:
    push    es
    mov     ax, 40h
    mov     es, ax
    mov     eax, [es:6ch]
    movzx   ebx, bx
    add     ebx, eax
.w:
    mov     eax, [es:6ch]
    cmp     eax, ebx
    jb      .w
    pop     es
    ret

section .data

lastax: dw 0

result:
    db "PLAY("
o_play: db "XXXX"
    db ") Q1("
o_q1:   db "XXXXXXXXXXXXXXXXXXXXXXXXXX"
    db ") Q2("
o_q2:   db "XXXXXXXXXXXXXXXXXXXXXXXXXX"
    db ") PAUSE("
o_pause: db "XXXX"
    db ") ST1("
o_st1:  db "XXXXXXXXXXXXXXXXXXXXXXXXXX"
    db ") Q3("
o_q3:   db "XXXXXXXXXXXXXXXXXXXXXXXXXX"
    db ") Q4("
o_q4:   db "XXXXXXXXXXXXXXXXXXXXXXXXXX"
    db ") RESUME("
o_resume: db "XXXX"
    db ") ST2("
o_st2:  db "XXXXXXXXXXXXXXXXXXXXXXXXXX"
    db ") ST3("
o_st3:  db "XXXXXXXXXXXXXXXXXXXXXXXXXX"
    db ")",13,10,'$'

section .bss
buf:    resb 32
req:    resb 32
""")

    results = self.runDosemu("testit.bat", config=config, timeout=10)

    names = ["PLAY", "Q1", "Q2", "PAUSE", "ST1", "Q3", "Q4", "RESUME",
             "ST2", "ST3"]
    pat = r" ".join(r"%s\(([0-9A-F]+)\)" % n for n in names)
    self.assertRegex(results, pat)
    v = dict(zip(names, re.search(pat, results).groups()))

    def ax(s):
        return int(s[:4], 16)

    def blk(s):
        return bytes.fromhex(s[4:])    # reply bytes 0..10 (byte 0 = request code)

    # play accepted
    self.assertEqual(ax(v["PLAY"]) & 0xff, 0, results)
    # Q1: just started, still in the hidden pregap of track 1: index 0,
    # busy flag in AH, relative time below 00:02:00 and absolute near 00:02
    q1 = blk(v["Q1"])
    self.assertEqual(ax(v["Q1"]) >> 8, 1, results)
    self.assertEqual((q1[2], q1[3]), (1, 0), results)
    self.assertLessEqual(q1[4] * 60 + q1[5], 2, results)
    # Q2: well past INDEX 01 of track 1
    q2 = blk(v["Q2"])
    self.assertEqual((q2[2], q2[3]), (1, 1), results)
    rel2 = (q2[4] * 60 + q2[5]) * 75 + q2[6]
    self.assertTrue(0 < rel2 < 300, results)
    # pause is accepted, and the head is frozen
    self.assertEqual(ax(v["PAUSE"]) & 0xff, 0, results)
    self.assertEqual(blk(v["ST1"])[1:3], b"\x01\x00", results)  # paused word
    self.assertEqual(blk(v["Q3"])[8:11], blk(v["Q4"])[8:11], results)
    # resume works from paused
    self.assertEqual(ax(v["RESUME"]), 0x0100, results)
    # after a stop the paused flag is clear
    self.assertEqual(blk(v["ST2"])[1:3], b"\x00\x00", results)
    # a short range finishes by itself: not busy any more
    self.assertEqual(ax(v["ST3"]) >> 8, 0, results)


def mscdex_image_audio(self):
    _run_audio(self)


def mscdex_image_sound(self):
    # Same run with sound on and the output captured to a wav file: the
    # first two seconds are the 220 Hz tone stored in the hidden pregap of
    # track 1 (LBA 0..149), so a wrong sector, byte order or rate shows.
    import struct
    import wave

    wav = Path(self.imagedir) / "cdaudio.wav"
    wav.unlink(missing_ok=True)
    _run_audio(self, """\
$_sound = (on)
$_sound_driver = "ao"
$_wav_file = "%s"
""" % wav)
    if not wav.exists():
        raise SkipTest("no sound output available (libao wav writer)")
    w = wave.open(str(wav))
    self.assertEqual((w.getnchannels(), w.getsampwidth(), w.getframerate()),
                     (2, 2, 44100))
    n = w.getnframes()
    data = struct.unpack("<%dh" % (n * 2), w.readframes(n))[0::2]
    first = next((i for i, x in enumerate(data) if x), None)
    self.assertIsNotNone(first, "only silence was written")

    # zero crossings over 1.5 s of the pregap tone: 220 Hz -> 440 per s
    seg = data[first:first + 66150]
    self.assertEqual(len(seg), 66150)
    zc = sum(1 for i in range(1, len(seg)) if (seg[i - 1] < 0) != (seg[i] < 0))
    freq = zc / 2 / 1.5
    self.assertAlmostEqual(freq, 220, delta=5, msg=str(freq))


def _cd_files(self, image, label):
    # Read a file off the data track of an image with a real DOS CD
    # extension.  SHSUCDX talks to dosemu2's cdrom.sys, parses the ISO 9660
    # file system itself and so exercises directory and file reads through
    # the image, not just single sectors.
    out = _make_images(self)
    pkg = Path(self.topdir) / BINSDIR / "shsucdx.zip"
    content = subprocess.check_output(
        ["unzip", "-p", "-C", str(pkg), "bin/shsucdx.com"])
    (Path(self.workdir) / "shsucdx.com").write_bytes(content)

    config = """\
$_hdimage = "dXXXXs/c:hdtype1 +1"
$_floppy_a = ""
$_cdrom = "%s"
""" % (out / image)

    self.mkfile("testit.bat", """\
c:\\shsucdx /d:mscd0001 /l:g
g:
dir
type hello.txt
copy big.dat c:\\bigcopy.dat
c:
rem end
""", newline="\r\n")

    results = self.runDosemu("testit.bat", config=config)
    self.assertIn("HELLO", results, "%s: file not listed\n%s" % (label, results))
    self.assertIn("hello from the cd image test", results,
                  "%s: file not read\n%s" % (label, results))

    # a file spanning ten sectors must come back byte for byte
    want = bytes((i * 7 + (i >> 8) * 13 + 1) & 0xff for i in range(20000))
    got = (Path(self.workdir) / "bigcopy.dat").read_bytes()
    self.assertEqual(len(got), len(want), "%s: BIG.DAT size\n%s" % (label, results))
    self.assertEqual(got, want, "%s: BIG.DAT contents differ" % label)


def mscdex_image_files_cue(self):
    _cd_files(self, "simple/simple.cue", "BIN/CUE, data track first")


def mscdex_image_files_multi(self):
    _cd_files(self, "multi/multi.cue", "BIN/CUE, one BIN per track")


def mscdex_image_files_chd(self):
    if not shutil.which("chdman"):
        raise SkipTest("needs chdman to build a CHD")
    _cd_files(self, "simple/simple.chd", "CHD")


def mscdex_image_real(self):
    # Optional: point DOSEMU_TEST_CD_IMAGES at one or more real .cue/.chd
    # files (space separated).  For each, SHSUCDX must mount it and DIR of
    # the root must list something.  Nothing from these images is stored in
    # the repository.  With DOSEMU_TEST_CD_COPY=DIR\\FILE (a path in the
    # image of the first one) and DOSEMU_TEST_CD_SHA256 that file is also
    # copied out and its hash compared.
    import hashlib
    import os
    images = os.environ.get("DOSEMU_TEST_CD_IMAGES", "").split()
    if not images:
        raise SkipTest("set DOSEMU_TEST_CD_IMAGES to try real discs")
    pkg = Path(self.topdir) / BINSDIR / "shsucdx.zip"
    content = subprocess.check_output(
        ["unzip", "-p", "-C", str(pkg), "bin/shsucdx.com"])
    (Path(self.workdir) / "shsucdx.com").write_bytes(content)
    copy = os.environ.get("DOSEMU_TEST_CD_COPY")
    want = os.environ.get("DOSEMU_TEST_CD_SHA256")
    for n, image in enumerate(images):
        config = """\
$_hdimage = "dXXXXs/c:hdtype1 +1"
$_floppy_a = ""
$_cdrom = "%s"
""" % image
        cmd = "copy g:\\%s c:\\copied.dat\r\n" % copy if copy and n == 0 else ""
        self.mkfile("testit.bat", """\
c:\\shsucdx /d:mscd0001 /l:g
g:
dir
%sc:
rem end
""" % cmd, newline="\r\n")
        results = self.runDosemu("testit.bat", config=config)
        self.assertRegex(results, r"([1-9]\d*) (file|dir)\(s\)",
                         "%s\n%s" % (image, results))
        self.assertNotIn("Invalid", results, image)
        print("\n--- %s\n%s" % (image, results))
        if cmd:
            got = (Path(self.workdir) / "copied.dat").read_bytes()
            digest = hashlib.sha256(got).hexdigest()
            print("copied %s: %d bytes sha256 %s" % (copy, len(got), digest))
            if want:
                self.assertEqual(digest, want, results)
