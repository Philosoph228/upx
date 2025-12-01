#! /usr/bin/env python3
import re

INPUT_FILE = "./src/main.cpp"
OUTPUT_FILE = "./src/longopts.gen.h"

def parse_longopts():
    with open(INPUT_FILE, "r", encoding="utf-8") as f:
        content = f.read()

    # Find main_get_options function
    main_func_match = re.search(r"int\s+main_get_options\s*\([^)]*\)\s*\{(.*?)\n\}", content, re.DOTALL)
    if not main_func_match:
        raise RuntimeError("main_get_options function not found")
    main_func_body = main_func_match.group(1)

    # Find longopts array
    longopts_match = re.search(r"static\s+const\s+struct\s+mfx_option\s+longopts\s*\[\]\s*=\s*\{(.*?)\};", main_func_body, re.DOTALL)
    if not longopts_match:
        raise RuntimeError("longopts array not found")
    longopts_body = longopts_match.group(1)

    # Regex to capture entries: {"name", flags, N, value}, optional comments ignored
    entry_pattern = re.compile(
        r'\{\s*"(?P<name>[^"]+)"\s*,\s*[^,]+,\s*[^,]+,\s*(?P<value>[^}]+)\}', re.MULTILINE
    )

    entries = []
    seen_values = set()
    for match in entry_pattern.finditer(longopts_body):
        name = match.group("name")
        value = match.group("value").strip()
        # Normalize the name: uppercase + hyphens to underscores
        macro_name = "LONGOPT_" + name.upper().replace("-", "_")
        # Avoid duplicate numeric values
        if value not in seen_values:
            entries.append((macro_name, value))
            seen_values.add(value)

    return entries

def write_header(entries):
    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write("// This file is auto-generated from main.cpp\n\n")
        f.write("#pragma once\n\n")
        for name, value in entries:
            f.write(f"#define {name} {value}\n")

def main():
    entries = parse_longopts()
    write_header(entries)
    print(f"Generated {OUTPUT_FILE} with {len(entries)} longopts.")

if __name__ == "__main__":
    main()
