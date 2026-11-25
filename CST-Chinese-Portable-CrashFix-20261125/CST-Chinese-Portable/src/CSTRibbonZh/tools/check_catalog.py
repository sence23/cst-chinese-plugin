"""Validate shipped UTF-8 source/translation catalogs before packaging."""
from pathlib import Path
import re

TOKENS = re.compile(r"%(?:[1-9][0-9]*![^!\r\n]+!|(?:[1-9][0-9]*\$)?[-+ #0']*"
                    r"(?:\*|[0-9]+)?(?:\.(?:\*|[0-9]+))?(?:hh|ll|I32|I64|[hljztL])?"
                    r"[diuoxXfFeEgGaAcspn]|L?[1-9][0-9]*|L?n|%|[^\s]*)")

def main():
    seen = {}
    total = 0
    for file in sorted((Path(__file__).resolve().parents[1] / "i18n").glob("*.tsv")):
        if file.name == "user.tsv":
            continue
        for number, line in enumerate(file.read_text("utf-8-sig").splitlines(), 1):
            if not line.strip() or line.startswith("#"):
                continue
            fields = line.split("\t")
            assert len(fields) == 2, (file.name, number, "Two columns required")
            source, target = fields
            assert source.strip() and target.strip() and "\ufffd" not in line
            assert len(source) <= 4096 and len(target) <= 8192, (file.name, number, "Entry too long")
            assert TOKENS.findall(source) == TOKENS.findall(target), (file.name, number, "Changed placeholders")
            assert not re.search(r"[<>&]", target), (file.name, number, "Markup/mnemonic in target")
            key = " ".join(source.split()).casefold()
            assert key not in seen, (file.name, number, "Duplicate", key)
            seen[key] = target
            total += 1
    assert total > 0
    print(f"PASS {total} unique UTF-8 catalog entries, consistent placeholders, no duplicate keys or target markup")

if __name__ == "__main__":
    main()
