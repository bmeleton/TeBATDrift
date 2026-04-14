#!/usr/bin/env python3

import os
import sys
import numpy as np
import matplotlib.pyplot as plt

"""
Usage:

python3 scripts/plot_drift_summary.py output/driftSim.out

"""


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 scripts/plot_drift_summary.py output/driftSim.out")
        sys.exit(1)

    infile = sys.argv[1]

    if not os.path.isfile(infile):
        print(f"ERROR: file not found: {infile}")
        sys.exit(1)

    data = np.loadtxt(infile)

    if data.ndim == 1:
        data = data.reshape(1, -1)

    # Columns match the old driftSim.out convention:
    #   0 = height [cm]
    #   1 = mean drift time [ns]
    #   2 = sigma_x [cm]
    #   3 = sigma_z [cm]
    #   4 = sigma_t [ns]

    height = data[:, 0]
    mean_t = data[:, 1]
    sigma_x = data[:, 2]
    sigma_z = data[:, 3]
    sigma_t = data[:, 4]

    # ------------------------------------------------------------------
    # 1. Drift velocity from linear fit to mean drift time vs height
    #    mean_t = m * height + b
    #    drift velocity = 1 / m  [cm/ns]
    #    convert to mm/us by multiplying by 1e4
    # ------------------------------------------------------------------
    m_time, b_time = np.polyfit(height, mean_t, 1)
    drift_velocity_mm_per_us = (1.0 / m_time) * 1.0e4

    print(f"Drift velocity = {drift_velocity_mm_per_us:.8f} mm/us")
    print(f"Linear fit intercept = {b_time:.8f} ns")

    # ------------------------------------------------------------------
    # 2. Fit sigma_t = a_t * sqrt(z)
    #    Fit by least squares on sigma_t^2 = a_t^2 * z + intercept
    # ------------------------------------------------------------------
    slope_t, intercept_t = np.polyfit(height, sigma_t**2, 1)
    a_t = np.sqrt(max(slope_t, 0.0))

    print(f"Drift Time Resolution at 1 cm = {a_t:.8f} ns")
    print(f"Time-resolution fit intercept = {intercept_t:.8f} ns^2")

    # ------------------------------------------------------------------
    # 3. Fit sigma_x = a_x * sqrt(z)
    # ------------------------------------------------------------------
    slope_x, intercept_x = np.polyfit(height, sigma_x**2, 1)
    a_x = np.sqrt(max(slope_x, 0.0))

    print(f"Drift Position Resolution at 1 cm = {a_x:.8f} cm")
    print(f"Position-resolution fit intercept = {intercept_x:.8f} cm^2")

    # Create output directory for plots next to the input file.
    outdir = os.path.dirname(infile)
    if outdir == "":
        outdir = "."
    os.makedirs(outdir, exist_ok=True)

    # ------------------------------------------------------------------
    # Plot 1: mean drift time vs height
    # ------------------------------------------------------------------
    plt.figure(figsize=(8, 6))
    plt.plot(height, mean_t, "o", label="Data")
    xfit = np.linspace(height.min(), height.max(), 200)
    plt.plot(xfit, m_time * xfit + b_time, "-", label="Linear fit")
    plt.xlabel("Drift Height [cm]")
    plt.ylabel("Mean Drift Time [ns]")
    plt.title("Mean Drift Time vs Drift Height")
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, "mean_drift_time_vs_height.png"), dpi=150)
    plt.close()

    # ------------------------------------------------------------------
    # Plot 2: sigma_t vs height
    # ------------------------------------------------------------------
    plt.figure(figsize=(8, 6))
    plt.plot(height, sigma_t, "o", label="Data")
    plt.plot(xfit, a_t * np.sqrt(xfit), "-", label=r"Fit: $a_t \sqrt{z}$")
    plt.xlabel("Drift Height [cm]")
    plt.ylabel("Drift Time Sigma [ns]")
    plt.title("Drift Time Resolution vs Drift Height")
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, "time_resolution_vs_height.png"), dpi=150)
    plt.close()

    # ------------------------------------------------------------------
    # Plot 3: sigma_x vs height
    # ------------------------------------------------------------------
    plt.figure(figsize=(8, 6))
    plt.plot(height, sigma_x, "o", label="Data")
    plt.plot(xfit, a_x * np.sqrt(xfit), "-", label=r"Fit: $a_x \sqrt{z}$")
    plt.xlabel("Drift Height [cm]")
    plt.ylabel("Drift Position Sigma [cm]")
    plt.title("Drift Position Resolution vs Drift Height")
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, "position_resolution_vs_height.png"), dpi=150)
    plt.close()

    # Optional extra plot for sigma_z
    plt.figure(figsize=(8, 6))
    plt.plot(height, sigma_z, "o-", label="Sigma_z")
    plt.xlabel("Drift Height [cm]")
    plt.ylabel("Drift Z Sigma [cm]")
    plt.title("Endpoint Z Sigma vs Drift Height")
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, "z_resolution_vs_height.png"), dpi=150)
    plt.close()

    print(f"Plots written to: {os.path.abspath(outdir)}")

if __name__ == "__main__":
    main()
