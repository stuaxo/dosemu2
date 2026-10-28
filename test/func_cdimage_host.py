from subprocess import check_call


def cdimage_host(self):
    # The cdimage layer on the host, no DOS involved
    check_call(["make", "--quiet", "-C", "test/cdimage", "clean", "check"])
