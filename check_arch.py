import pefile
import sys

def arch(path):
    pe = pefile.PE(path)
    m = pe.FILE_HEADER.Machine
    if m == 0x8664:
        return 'x64'
    if m == 0x14c:
        return 'i386'
    return hex(m)

for p in sys.argv[1:]:
    print(p, arch(p))
