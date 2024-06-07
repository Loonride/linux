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

def link_bitcode_files(filenames, outfile):
    result = subprocess.run(['llvm-link'] + filenames + ['-o', outfile], capture_output=True, text=True, check=True)
    stdout = result.stdout.strip()

def transform_bitcode_file(input_file, output_file):
    transform_out_path = "/home/kir/beandip/linux-qemu/lto_kernel/transform.log"
    with open(transform_out_path, "w") as transform_out:
        subprocess.run(['/home/kir/beandip/beandip/local/bin/beandip-transform-no-runtime', input_file, output_file], stdout=transform_out, stderr=transform_out)

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

    # print(f"elf file count: {len(elf_files)}")
    # print(f"bc file count: {len(bc_files)}")

    bc_outfile = "/home/kir/beandip/linux-qemu/lto_kernel/linux.bc"

    link_bitcode_files(bc_files, bc_outfile)

    bc_transformed = "/home/kir/beandip/linux-qemu/lto_kernel/transformed.bc"
    transform_bitcode_file(bc_outfile, bc_transformed)

    all_files = [bc_transformed]
    # all_files.extend(bc_files)
    all_files.extend(orig_elf_files)
    all_files.extend(elf_files)

    full_str = " ".join(all_files)

    print(full_str)
