#!/usr/bin/env python3

import glob
import os
import sys
import numpy as np

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 scripts/merge_drift_outputs.py 'output/drift_part_*.out' output/driftSim.out")
        sys.exit(1)

    pattern = sys.argv[1]
    outfile = sys.argv[2]

    files = sorted(glob.glob(pattern))
    if not files:
        print(f"ERROR: no files matched pattern: {pattern}")
        sys.exit(1)

    rows = []
    for f in files:
        try:
          data = np.loadtxt(f)
        except Exception as e:
          print(f"ERROR reading {f}: {e}")
          sys.exit(1)

        if data.size == 0:
            continue

        if data.ndim == 1:
            data = data.reshape(1, -1)

        rows.append(data)

    if not rows:
        print("ERROR: no data found in matched files.")
        sys.exit(1)

    merged = np.vstack(rows)

    # Sort by height column (column 0).
    merged = merged[np.argsort(merged[:, 0])]

    outdir = os.path.dirname(outfile)
    if outdir:
        os.makedirs(outdir, exist_ok=True)

    np.savetxt(outfile, merged, fmt="%.10g")
    print(f"Merged {len(files)} files into: {outfile}")

if __name__ == "__main__":
    main()
