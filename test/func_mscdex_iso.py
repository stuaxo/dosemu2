import shutil
import subprocess
from pathlib import Path

from common_framework import install_shsucdx


def mscdex_nul_redirect_iso(self):
    # Redirecting to "nul:" (with the trailing colon) while the current
    # drive is a CD served by a DOS CD extension used to fail with
    # "Redirection to nul: failed". The drive's redirector does not know
    # the name, and the local mapper in the kernel did not either. A
    # drive that dosemu2 itself redirects resolves the name before that
    # point, so the bug only shows with a real CD extension; the big red
    # racing installer, which writes ">nul:" on every line, hit it.
    #
    # Only the redirection is under test, so the commands read nothing
    # from the disc.
    mkiso = shutil.which("genisoimage") or shutil.which("mkisofs")
    if not mkiso:
        self.skipTest("needs genisoimage or mkisofs")
    install_shsucdx(self)

    cdroot = Path(self.imagedir) / "nulcd"
    cdroot.mkdir(exist_ok=True)
    (cdroot / "README.TXT").write_bytes(b"test\r\n")
    iso = Path(self.imagedir) / "nulcd.iso"
    subprocess.run([mkiso, "-quiet", "-o", str(iso), str(cdroot)],
                   check=True, capture_output=True)

    config = """\
$_hdimage = "dXXXXs/c:hdtype1 +1"
$_floppy_a = ""
$_cdrom = "%s"
""" % iso

    self.mkfile("testit.bat", """\
c:\\shsucdx /d:mscd0001 /l:g
g:
md c:\\nb >nul:
copy c:\\testit.bat c:\\copy.bat >nul:
c:
rem end
""", newline="\r\n")

    results = self.runDosemu("testit.bat", config=config)

    if "SHSUCDX can't install" in results:
        self.skipTest("no CD driver for the image on this DOS")

    self.assertNotIn("Redirection to nul: failed", results)

    # the case of the names depends on the DOS
    names = {p.name.lower() for p in Path(self.workdir).iterdir()}
    self.assertIn("nb", names, "md with >nul: did not run\n%s" % results)
    self.assertIn("copy.bat", names,
                  "copy with >nul: did not run\n%s" % results)
