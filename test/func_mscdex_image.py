import re
import shutil
import subprocess
import sys
from pathlib import Path
from unittest import SkipTest


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
