#!/usr/bin/env python3
import sys
import subprocess

RUN_BEANDIP_TRANFORM = False
LINUX_DIR = "/home/kir/beandip/linux-riscv/milkv/linux"
BEANDIP_DIR = "/home/kir/beandip/beandip"

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
    transform_out_path = f"{LINUX_DIR}/transform.log"
    with open(transform_out_path, "w") as transform_out:
        subprocess.run([f'{BEANDIP_DIR}/local/bin/beandip-transform-no-runtime', input_file, output_file], stdout=transform_out, stderr=transform_out)

def llvm_dis(input, output):
    result = subprocess.run(['llvm-dis', input, '-o', output], capture_output=True, text=True, check=True)
    stdout = result.stdout.strip()

def llvm_as(input, output):
    result = subprocess.run(['llvm-as', input, '-o', output], capture_output=True, text=True, check=True)
    stdout = result.stdout.strip()

if __name__ == "__main__":
    orig_elf_files = []
    l = run_nm("vmlinux.a")
    # orig_elf_files = []
    # l = []
    # for arg in args:
    #     ext = arg.split(".")[1]
    #     if ext == "a":
    #         l.extend(run_nm(arg))
    #     elif ext == "o":
    #         orig_elf_files.append(arg)

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

    bc_outfile = f"{LINUX_DIR}/linux.bc"

    link_bitcode_files(bc_files, bc_outfile)

    all_files = []

    if RUN_BEANDIP_TRANFORM:
        bc_transformed_tmp = f"{LINUX_DIR}/transformed.tmp.bc"
        ll_transformed_tmp = f"{LINUX_DIR}/transformed.tmp.ll"

        transform_bitcode_file(bc_outfile, bc_transformed_tmp)

        llvm_dis(bc_transformed_tmp, ll_transformed_tmp)

        # Weird hack to make the linker happy when building tinyconfig Linux
        joined = ""
        with open(ll_transformed_tmp, 'r') as f:
            raw_file = f.read()
            lines = raw_file.split("\n")
            new_lines = []
            for l in lines:
                if l == 'module asm ".weak sys_clock_adjtime32"':
                    new_lines.append('module asm ".globl sys_clock_adjtime32"')
                else:
                    new_lines.append(l)

            joined = "\n".join(new_lines)

        bc_transformed = f"{LINUX_DIR}/transformed.bc"
        ll_transformed = f"{LINUX_DIR}/transformed.ll"

        with open(ll_transformed, 'w') as f:
            f.write(joined)

        llvm_as(ll_transformed, bc_transformed)

        all_files.append(bc_transformed)
    else:
        all_files.append(bc_outfile)

    # all_files.extend(bc_files)
    all_files.extend(orig_elf_files)
    all_files.extend(elf_files)

    # full_str = " ".join(all_files)

    args = sys.argv[1:]

    placeholder_arg_idx = -1
    for (idx, arg) in enumerate(args):
        if arg == "--":
            placeholder_arg_idx = idx
            break

    first_half = args[:placeholder_arg_idx]
    last_half = args[(placeholder_arg_idx + 1):]

    print(first_half)
    print(all_files)
    print(last_half)
    ld_args = first_half + all_files + last_half
    print(" ".join(ld_args))
    result = subprocess.run(ld_args, capture_output=True, text=True, check=True)
