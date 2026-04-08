#!/usr/bin/env python3
"""
Multi-seed validation sweep.
Changes the LP weight seed, builds, flashes, captures Tests 12-14 output.
"""

import subprocess, serial, time, sys, re, os

SEEDS = [0xCAFE1234, 0xDEAD5678, 0xBEEF9ABC]
SRC = 'embedded/main/geometry_cfc_freerun.c'
PORT = '/dev/esp32c6a'
IDF_ENV = 'source /home/ztflynn/esp/v5.4/export.sh >/dev/null 2>&1'
RESULTS = {}

def run_shell(cmd, timeout=300):
    r = subprocess.run(f'bash -c \'{IDF_ENV} && {cmd}\'',
                       shell=True, capture_output=True, text=True, timeout=timeout)
    return r.stdout + r.stderr

def set_seed(seed):
    with open(SRC, 'r') as f:
        content = f.read()
    content = re.sub(
        r'init_lp_core_weights\(0x[0-9A-Fa-f]+\)',
        f'init_lp_core_weights(0x{seed:08X})',
        content
    )
    with open(SRC, 'w') as f:
        f.write(content)
    print(f'  Set seed to 0x{seed:08X}')

def build_and_flash():
    print('  Building...')
    out = run_shell('cd embedded && idf.py -DREFLEX_TARGET=gie build 2>&1')
    if 'error:' in out.lower():
        print(f'  BUILD FAILED')
        print(out[-500:])
        return False
    print('  Flashing...')
    out = run_shell(f'cd embedded && idf.py -p {PORT} flash 2>&1')
    if 'Done' not in out:
        print(f'  FLASH FAILED')
        return False
    print('  OK')
    return True

def reset_board():
    run_shell(f'python3 -m esptool --port {PORT} --chip esp32c6 --after hard_reset read_mac 2>&1')
    time.sleep(1)

def capture_output(timeout_s=900):
    ser = serial.Serial(PORT, 115200, timeout=1)
    start = time.time()
    lines = []
    while time.time() - start < timeout_s:
        data = ser.readline()
        if data:
            line = data.decode('utf-8', errors='replace').strip()
            lines.append(line)
            if 'RESULTS:' in line:
                for _ in range(80):
                    data = ser.readline()
                    if data:
                        lines.append(data.decode('utf-8', errors='replace').strip())
                break
    ser.close()
    return lines

def extract_results(lines):
    """Extract key metrics from test output."""
    results = {}

    # Find RESULTS line
    for l in lines:
        if 'RESULTS:' in l:
            results['summary'] = l.strip()

    # TEST 12: P1 vs P2 sign and MTFP
    for l in lines:
        m = re.search(r'P1 vs P2: sign=(-?\d+)/16, mtfp=(-?\d+)/80', l)
        if m:
            results['t12_p1p2_sign'] = int(m.group(1))
            results['t12_p1p2_mtfp'] = int(m.group(2))
        m = re.search(r'P1 vs P3: sign=(-?\d+)/16, mtfp=(-?\d+)/80', l)
        if m:
            results['t12_p1p3_sign'] = int(m.group(1))
            results['t12_p1p3_mtfp'] = int(m.group(2))

    # TEST 13: CMD 4 ablation
    for l in lines:
        m = re.search(r'CMD 5 \(TEST 12\) P1 vs P2 Hamming: (\d+)', l)
        if m:
            results['t13_cmd5_p1p2'] = int(m.group(1))
        m = re.search(r'CMD 4 \(TEST 13\) P1 vs P2 Hamming: (\d+)', l)
        if m:
            results['t13_cmd4_p1p2'] = int(m.group(1))

    # TEST 14: mean Hamming per condition
    for l in lines:
        m = re.search(r'Mean\s+\|\s+([\d.]+)\s+\|\s+([\d.]+)\s+\|', l)
        if m:
            results['t14_mean_14a'] = float(m.group(1))
            results['t14_mean_14c'] = float(m.group(2))

    # TEST 14: gate bias activated
    for l in lines:
        m = re.search(r'Gate bias activated.*max=(\d+)', l)
        if m:
            results['t14_max_bias'] = int(m.group(1))

    # Split-half null
    for l in lines:
        if 'SIGNAL > NULL' in l:
            results['null_test'] = 'SIGNAL > NULL'
        elif 'WARNING' in l and 'noise floor' in l:
            results['null_test'] = 'WARNING'

    return results

def main():
    print('=== Multi-Seed Validation Sweep ===\n')

    for seed in SEEDS:
        print(f'\n--- Seed 0x{seed:08X} ---')
        set_seed(seed)
        if not build_and_flash():
            print('  SKIPPING')
            continue
        reset_board()
        print('  Capturing test output...')
        lines = capture_output()

        # Save raw output
        outfile = f'/tmp/reflex_seed_{seed:08X}.txt'
        with open(outfile, 'w') as f:
            for l in lines:
                f.write(l + '\n')
        print(f'  Saved to {outfile}')

        results = extract_results(lines)
        RESULTS[seed] = results
        print(f'  Results: {results}')

    # Restore original seed
    set_seed(SEEDS[0])

    # Print summary table
    print('\n\n=== SUMMARY TABLE ===\n')
    print(f'{"Metric":<30} ', end='')
    for s in SEEDS:
        print(f'  0x{s:08X}', end='')
    print()
    print('-' * 70)

    metrics = [
        ('T12 P1-P2 sign (/16)', 't12_p1p2_sign'),
        ('T12 P1-P2 MTFP (/80)', 't12_p1p2_mtfp'),
        ('T12 P1-P3 sign (/16)', 't12_p1p3_sign'),
        ('T12 P1-P3 MTFP (/80)', 't12_p1p3_mtfp'),
        ('T13 CMD5 P1-P2', 't13_cmd5_p1p2'),
        ('T13 CMD4 P1-P2', 't13_cmd4_p1p2'),
        ('T14 mean 14A', 't14_mean_14a'),
        ('T14 mean 14C', 't14_mean_14c'),
        ('T14 max bias', 't14_max_bias'),
        ('Null test', 'null_test'),
    ]

    for label, key in metrics:
        print(f'{label:<30} ', end='')
        for s in SEEDS:
            val = RESULTS.get(s, {}).get(key, '-')
            print(f'  {str(val):>10}', end='')
        print()

    # Save summary
    with open('/tmp/reflex_seed_summary.txt', 'w') as f:
        f.write('=== Multi-Seed Validation Summary ===\n\n')
        f.write(f'{"Metric":<30} ')
        for s in SEEDS:
            f.write(f'  0x{s:08X}')
        f.write('\n' + '-' * 70 + '\n')
        for label, key in metrics:
            f.write(f'{label:<30} ')
            for s in SEEDS:
                val = RESULTS.get(s, {}).get(key, '-')
                f.write(f'  {str(val):>10}')
            f.write('\n')

    print('\nSaved to /tmp/reflex_seed_summary.txt')
    print('\nDone.')

if __name__ == '__main__':
    main()
