#!/usr/bin/env python3
import os
import sys
import subprocess
import glob

def main():
    test_dir = "tests/fyra"
    test_files = sorted(glob.glob(os.path.join(test_dir, "*.fyra")))

    print(f"Found {len(test_files)} Fyra test files in {test_dir}.")

    # Force rebuild compiler first
    print("Building compiler...")
    if os.path.exists("build/bin/fyra_compiler"):
        os.remove("build/bin/fyra_compiler")
    if os.path.exists("build/libfyra.a"):
        os.remove("build/libfyra.a")
    subprocess.run(["make"], check=True)

    passed_tests = []
    failed_tests = []

    # Ensure compiler exists
    compiler_path = "build/bin/fyra_compiler"
    if not os.path.exists(compiler_path):
        print(f"Error: Compiler not found at {compiler_path}. Please build it first.")
        sys.exit(1)

    wrapper_c = os.path.join(test_dir, "main_wrapper.c")
    if not os.path.exists(wrapper_c):
        print(f"Error: Wrapper C file not found at {wrapper_c}.")
        sys.exit(1)

    for test_file in test_files:
        test_name = os.path.basename(test_file)
        print(f"\n--- Running test: {test_name} ---")

        # Step 1: Compile to .s
        asm_file = "temp.s"
        if os.path.exists(asm_file):
            os.remove(asm_file)

        compile_cmd = [compiler_path, test_file, "-o", asm_file, "--target", "x64-linux-bin"]
        print(f"Compiling: {' '.join(compile_cmd)}")
        try:
            res = subprocess.run(compile_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=10)
            if res.returncode != 0:
                print(f"Compilation FAILED for {test_file}:")
                print(res.stdout)
                print(res.stderr)
                failed_tests.append((test_name, "Compilation failed"))
                continue
        except subprocess.TimeoutExpired:
            print("Compilation TIMEOUT")
            failed_tests.append((test_name, "Compilation timeout"))
            continue

        # Step 2: Link with gcc
        exec_file = "./temp_exec"
        if os.path.exists(exec_file):
            os.remove(exec_file)

        link_cmd = ["gcc", "-no-pie", wrapper_c, asm_file, "-o", exec_file]
        print(f"Linking: {' '.join(link_cmd)}")
        res = subprocess.run(link_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res.returncode != 0:
            print(f"Linking FAILED for {test_file}:")
            print(res.stdout)
            print(res.stderr)
            failed_tests.append((test_name, "Linking failed"))
            continue

        # Step 3: Execute
        print("Executing...")
        try:
            res = subprocess.run([exec_file], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=10)
            print("Output:")
            print(res.stdout)
            if res.stderr:
                print("Error output:")
                print(res.stderr)

            is_assertion_fail = "Assertion failed" in res.stderr or "Assertion failed" in res.stdout
            if res.returncode >= 0 and not is_assertion_fail and res.returncode != 1:
                print(f"Test {test_name} PASSED! (Exit code: {res.returncode})")
                passed_tests.append(test_name)
            else:
                print(f"Test {test_name} FAILED with exit code {res.returncode}")
                failed_tests.append((test_name, f"Execution failed (code {res.returncode})"))
        except subprocess.TimeoutExpired:
            print("Execution TIMEOUT")
            failed_tests.append((test_name, "Execution timeout"))

    # Cleanup temp files
    for f in ["temp.s", "temp_exec"]:
        if os.path.exists(f):
            try:
                os.remove(f)
            except:
                pass

    print("\n==========================================")
    print(f"Test Results: {len(passed_tests)} Passed, {len(failed_tests)} Failed.")
    print("==========================================")
    if passed_tests:
        print("Passed:")
        for t in passed_tests:
            print(f"  - {t}")
    if failed_tests:
        print("\nFailed:")
        for t, reason in failed_tests:
            print(f"  - {t} ({reason})")

    if failed_tests:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == "__main__":
    main()
