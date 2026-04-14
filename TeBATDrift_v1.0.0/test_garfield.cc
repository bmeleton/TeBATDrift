#include <iostream>
#include <string>

#include "Garfield/MediumMagboltz.hh"

int main() {
  Garfield::MediumMagboltz gas;

  gas.SetTemperature(293.15);
  gas.SetPressure(300.0);

  // 90% He, 10% CO2
  gas.SetComposition("he", 90., "co2", 10.);

  // Generate transport table over a modest electric-field range.
  // Arguments: Emin, Emax, nE, log-scale
  gas.SetFieldGrid(50., 100., 20, true);

  std::cout << "Generating gas table..." << std::endl;
  gas.GenerateGasTable(1, true);

  const std::string outfile = "he_co2_test.gas";
  gas.WriteGasFile(outfile);

  std::cout << "Wrote gas table to " << outfile << std::endl;
  return 0;
}
