import pefile
import sys

def version_info(path):
    try:
        pe = pefile.PE(path)
        st = pe.FileInfo[0][0].StringTable[0]
        entries = st.entries
        print("type entries:", type(entries))
        for k, v in entries.items():
            print(f"key type {type(k)} val type {type(v)}: {k!r} -> {v!r}")
    except Exception as e:
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    version_info(sys.argv[1])
