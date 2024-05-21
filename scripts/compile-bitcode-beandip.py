#!/usr/bin/env python3
import sys
import subprocess

def run_nm(filename):
    result = subprocess.run(['nm', filename], capture_output=True, text=True, check=True)
    stdout = result.stdout
    lines = stdout.split("\n")
    res = []
    for line in lines:
        line = line.strip()
        if len(line) < 3 or line[-3:] != ".o:":
            continue
        final = line.split(":")[0]
        res.append(final)

    return res

def is_file_bitcode(filename):
    result = subprocess.run(['file', filename], capture_output=True, text=True, check=True)
    stdout = result.stdout.strip()
    
    if "LLVM IR bitcode" in stdout:
        return True
    elif "ELF 64-bit LSB relocatable" in stdout:
        return False
    else:
        raise Exception(f"bad file: {filename}")

if __name__ == "__main__":
    args = sys.argv[1:]

    orig_elf_files = []
    l = []
    for arg in args:
        ext = arg.split(".")[1]
        if ext == "a":
            l.extend(run_nm(arg))
        elif ext == "o":
            orig_elf_files.append(arg)

    bc_files = []
    elf_files = []

    for fname in l:
        if is_file_bitcode(fname):
            bc_files.append(fname)
        else:
            elf_files.append(fname)
    
    # now we can transform bc_files here if we want

    all_files = []
    all_files.extend(orig_elf_files)
    all_files.extend(bc_files)
    all_files.extend(elf_files)

    full_str = " ".join(all_files)

    print(full_str)
