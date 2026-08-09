import pefile
import os

exe = r'e:\anchor\Trae\QDV\build\bin\QDV_tests.exe'
search_paths = [r'e:\anchor\Trae\QDV\build\bin'] + os.environ['PATH'].split(os.pathsep)

def find_dll(name):
    for d in search_paths:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    return None

def get_exports(path):
    try:
        pe = pefile.PE(path)
    except Exception:
        return set()
    if not hasattr(pe, 'DIRECTORY_ENTRY_EXPORT'):
        return set()
    return {exp.name.decode('utf-8', errors='ignore') for exp in pe.DIRECTORY_ENTRY_EXPORT.symbols if exp.name}

pe = pefile.PE(exe)
unresolved = []
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    dll = entry.dll.decode('utf-8', errors='ignore')
    loc = find_dll(dll)
    if not loc:
        continue
    exports = get_exports(loc)
    for imp in entry.imports:
        name = imp.name.decode('utf-8', errors='ignore') if imp.name else None
        if name and name not in exports:
            unresolved.append((dll, name))

print('Unresolved imports:')
for dll, name in unresolved[:50]:
    print(f'  {dll} -> {name}')
print('total unresolved', len(unresolved))
