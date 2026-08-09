import pefile
import os
import sys

exe = r'e:\anchor\Trae\QDV\build\bin\QDV_tests.exe'
search_paths = [r'e:\anchor\Trae\QDV\build\bin'] + os.environ['PATH'].split(os.pathsep)

missing = []
found_dlls = set()

def find_dll(name):
    for d in search_paths:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    return None

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
            found_dlls.add(dll)
            scan(loc, depth + 1)
        else:
            missing.append(dll)

scan(exe)
real_missing = [m for m in missing if not m.startswith('api-ms-win-')]
print('Missing real DLLs:')
for m in real_missing:
    print(' ', m)
if not real_missing:
    print('  None')
