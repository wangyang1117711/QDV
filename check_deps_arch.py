import pefile
import os
import sys

exe = r'e:\anchor\Trae\QDV\build\bin\QDV_tests.exe'
search_paths = [r'e:\anchor\Trae\QDV\build\bin'] + os.environ['PATH'].split(os.pathsep)

def find_dll(name):
    for d in search_paths:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    return None

def arch(path):
    try:
        pe = pefile.PE(path)
        m = pe.FILE_HEADER.Machine
        if m == 0x8664:
            return 'x64'
        if m == 0x14c:
            return 'i386'
        return hex(m)
    except Exception as e:
        return f'error:{e}'

found_dlls = {}
missing = []

def scan(path, depth=0):
    if depth > 5:
        return
    try:
        pe = pefile.PE(path)
    except Exception as e:
        print(f'Cannot parse {path}: {e}')
        return
    if not hasattr(pe, 'DIRECTORY_ENTRY_IMPORT'):
        return
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        dll = entry.dll.decode('utf-8', errors='ignore')
        if dll in found_dlls:
            continue
        loc = find_dll(dll)
        if loc:
            found_dlls[dll] = loc
            scan(loc, depth + 1)
        else:
            missing.append(dll)

scan(exe)
print('Dependency architecture summary:')
for dll, loc in sorted(found_dlls.items()):
    print(f'  {dll}: {arch(loc)} -> {loc}')
print('Missing real DLLs:')
real = [m for m in missing if not m.startswith('api-ms-win-')]
for m in real:
    print(' ', m)
if not real:
    print('  None')
