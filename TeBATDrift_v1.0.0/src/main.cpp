#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

// Garfield++ Transport Classes
#include "Garfield/AvalancheMC.hh"
#include "Garfield/ComponentConstant.hh"
#include "Garfield/GeometrySimple.hh"
#include "Garfield/MediumMagboltz.hh"
#include "Garfield/Sensor.hh"
#include "Garfield/SolidBox.hh"
#include "Garfield/ViewDrift.hh"

// ROOT GUI classes for optional interactive visualization
#include "TApplication.h"
#include "TCanvas.h"

// Configuration data loaded from JSON file
struct Config {
  double temperature = 293.15;
  double pressure = 300.0;
  double eField = 200.0;
  std::string gas1 = "he";
  double frac1 = 90.0;
  std::string gas2 = "co2";
  double frac2 = 10.0;
  std::string gasFile = "gas/default.gas";
  int iterations = 10000;
  double minHeight = 0.5;
  int numSteps = 20;
  double stepSize = 0.5;
  std::string outputFile = "output/driftSim.out";
};

// Runtime options that come from the command line rather than config file:
// These are mainly for:
//  - Running only a subset of the drift-height steps
//  - Overriding the output file
//  - Optionally turning on interactive Garfield/ROOT visualization
struct RunOptions {
  std::string configPath;     // Path to JSON config
  int startStep = -1;         //Step-range restrictions - if left negative, code uses full scan range from config
  int endStep = -1;
  std::string outputOverride; // Summary output override

  // Visualization options
  bool visualize = false; // If true, opens a ROOT canvas and draws drift lines
  int vizStep = 0;        // Which drift height step to visualize
  int vizElectrons = 100; // How many electron paths to draw
};

// Reads an entire text file into one string - used to read the JSON config file all at once
static std::string ReadWholeFile(const std::string& path) {
  std::ifstream in(path.c_str());
  if (!in) {
    throw std::runtime_error("Could not open config file: " + path);
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Extracts a floating-point value associated w/ a given JSON key (i.e. gets values from JSON)
static double ExtractDouble(const std::string& text, const std::string& key) {
  const std::regex re("\"" + key + "\"\\s*:\\s*([-+]?\\d*\\.?\\d+(?:[eE][-+]?\\d+)?)");
  std::smatch match;
  if (!std::regex_search(text, match, re)) {
    throw std::runtime_error("Could not parse double key \"" + key + "\" from config.");
  }
  return std::stod(match[1].str());
}

// Extracts integer values
static int ExtractInt(const std::string& text, const std::string& key) {
  const std::regex re("\"" + key + "\"\\s*:\\s*(\\d+)");
  std::smatch match;
  if (!std::regex_search(text, match, re)) {
    throw std::runtime_error("Could not parse int key \"" + key + "\" from config.");
  }
  return std::stoi(match[1].str());
}

// Extracts string values
static std::string ExtractString(const std::string& text, const std::string& key) {
  const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
  std::smatch match;
  if (!std::regex_search(text, match, re)) {
    throw std::runtime_error("Could not parse string key \"" + key + "\" from config.");
  }
  return match[1].str();
}

// Extracts the two-component gas mixture from JSON
static void ExtractComposition(const std::string& text,
                               std::string& gas1, double& frac1,
                               std::string& gas2, double& frac2) {
  const std::regex re(
      "\"composition\"\\s*:\\s*\\[\\s*\"([^\"]+)\"\\s*,\\s*"
      "([-+]?\\d*\\.?\\d+(?:[eE][-+]?\\d+)?)\\s*,\\s*"
      "\"([^\"]+)\"\\s*,\\s*"
      "([-+]?\\d*\\.?\\d+(?:[eE][-+]?\\d+)?)\\s*\\]");
  std::smatch match;
  if (!std::regex_search(text, match, re)) {
    throw std::runtime_error("Could not parse \"composition\" array from config.");
  }
  gas1 = match[1].str();
  frac1 = std::stod(match[2].str());
  gas2 = match[3].str();
  frac2 = std::stod(match[4].str());
}

// Load the user config from disk into a config structure
static Config LoadConfig(const std::string& path) {
  const std::string text = ReadWholeFile(path);

  Config cfg;
  cfg.temperature = ExtractDouble(text, "temperature");
  cfg.pressure    = ExtractDouble(text, "pressure");
  cfg.eField      = ExtractDouble(text, "eField");
  ExtractComposition(text, cfg.gas1, cfg.frac1, cfg.gas2, cfg.frac2);
  cfg.gasFile     = ExtractString(text, "gasFile");
  cfg.iterations  = ExtractInt(text, "iterations");
  cfg.minHeight   = ExtractDouble(text, "minHeight");
  cfg.numSteps    = ExtractInt(text, "numSteps");
  cfg.stepSize    = ExtractDouble(text, "stepSize");
  cfg.outputFile  = ExtractString(text, "outputFile");
  return cfg;
}

// Computes Mean
static double Mean(const std::vector<double>& v) {
  if (v.empty()) return 0.0;
  const double sum = std::accumulate(v.begin(), v.end(), 0.0);
  return sum / static_cast<double>(v.size());
}

// Computes Standard Deviation
static double Sigma(const std::vector<double>& v, const double mean) {
  if (v.empty()) return 0.0;
  double accum = 0.0;
  for (size_t i = 0; i < v.size(); ++i) {
    const double dx = v[i] - mean;
    accum += dx * dx;
  }
  return std::sqrt(accum / static_cast<double>(v.size()));
}

// Check whether file exists before opening it
static bool FileExists(const std::string& path) {
  std::ifstream f(path.c_str());
  return f.good();
}

// Return the parent directory portion of a path
static std::string GetParentDir(const std::string& filepath) {
  const std::string::size_type pos = filepath.find_last_of("/\\");
  if (pos == std::string::npos) return "";
  return filepath.substr(0, pos);
}

// Create a directory if it does not already exist
static void MakeDirIfNeeded(const std::string& dir) {
  if (dir.empty()) return;

  struct stat st;
  if (stat(dir.c_str(), &st) == 0) {
    if (S_ISDIR(st.st_mode)) return;
    throw std::runtime_error("Path exists but is not a directory: " + dir);
  }

  if (mkdir(dir.c_str(), 0755) != 0) {
    if (errno == EEXIST) return;
    throw std::runtime_error("Failed to create directory: " + dir +
                             " (errno=" + std::to_string(errno) + ")");
  }
}

// Ensures that the parent directory of a filepath exists.
static void EnsureParentDirExists(const std::string& filepath) {
  const std::string dir = GetParentDir(filepath);
  if (dir.empty()) return;

  std::stringstream ss(dir);
  std::string item;
  std::string current;

  while (std::getline(ss, item, '/')) {
    if (item.empty()) continue;
    if (!current.empty()) current += "/";
    current += item;
    MakeDirIfNeeded(current);
  }
}

// Build or load the Magboltz gas table
static void BuildOrLoadGas(Garfield::MediumMagboltz& gas, const Config& cfg) {
  gas.SetTemperature(cfg.temperature);
  gas.SetPressure(cfg.pressure);
  gas.EnableDrift();
  gas.SetComposition(cfg.gas1, cfg.frac1, cfg.gas2, cfg.frac2);

  if (FileExists(cfg.gasFile)) {
    std::cout << "Loading existing gas file: " << cfg.gasFile << "\n";
    gas.LoadGasFile(cfg.gasFile);
    return;
  }

  std::cout << "Gas file not found. Generating gas table: " << cfg.gasFile << "\n";
  EnsureParentDirExists(cfg.gasFile);

  gas.SetFieldGrid(cfg.eField, cfg.eField, 1, false);
  gas.GenerateGasTable(10, true);
  gas.WriteGasFile(cfg.gasFile);

  std::cout << "Gas table written to: " << cfg.gasFile << "\n";
}

// Prints the command line usage info back to user
static void PrintUsage() {
  std::cout
      << "Usage:\n"
      << "  ./GarfieldDriftRes config/drift_config.json\n"
      << "  ./GarfieldDriftRes config/drift_config.json --start-step N --end-step M --output output/part.out\n"
      << "  ./GarfieldDriftRes config/drift_config.json --visualize --viz-step N --viz-electrons M\n"
      << "\n"
      << "Optional arguments:\n"
      << "  --start-step N     First step index to run (0-based)\n"
      << "  --end-step M       Last step index to run (0-based, inclusive)\n"
      << "  --output PATH      Override output filename\n"
      << "  --visualize        Enable ROOT/Garfield drift-line display\n"
      << "  --viz-step N       Step index to visualize (default: 0)\n"
      << "  --viz-electrons M  Number of electrons to draw for that step (default: 100)\n";
}

// Parses command-line args into a RunOptions structure
static RunOptions ParseArguments(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage();
    throw std::runtime_error("Missing required config path.");
  }

  RunOptions opts;
  opts.configPath = argv[1];

  int i = 2;
  while (i < argc) {
    const std::string arg = argv[i];

    if (arg == "--start-step") {
      if (i + 1 >= argc) {
        throw std::runtime_error("Missing value after --start-step");
      }
      opts.startStep = std::stoi(argv[i + 1]);
      i += 2;
    } else if (arg == "--end-step") {
      if (i + 1 >= argc) {
        throw std::runtime_error("Missing value after --end-step");
      }
      opts.endStep = std::stoi(argv[i + 1]);
      i += 2;
    } else if (arg == "--output") {
      if (i + 1 >= argc) {
        throw std::runtime_error("Missing value after --output");
      }
      opts.outputOverride = argv[i + 1];
      i += 2;
    } else if (arg == "--visualize") {
      opts.visualize = true;
      i += 1;
    } else if (arg == "--viz-step") {
      if (i + 1 >= argc) {
        throw std::runtime_error("Missing value after --viz-step");
      }
      opts.vizStep = std::stoi(argv[i + 1]);
      i += 2;
    } else if (arg == "--viz-electrons") {
      if (i + 1 >= argc) {
        throw std::runtime_error("Missing value after --viz-electrons");
      }
      opts.vizElectrons = std::stoi(argv[i + 1]);
      i += 2;
    } else {
      throw std::runtime_error("Unknown argument: " + arg);
    }
  }

  return opts;
}

// Main
int main(int argc, char* argv[]) {
  try {
    // Parses command line options & loads JSON
    const RunOptions opts = ParseArguments(argc, argv);
    Config cfg = LoadConfig(opts.configPath);

    // Default behavior -r run full steps
    int startStep = 0;
    int endStep = cfg.numSteps - 1;

    // If command line overrides are provided, apply them
    if (opts.startStep >= 0) startStep = opts.startStep;
    if (opts.endStep >= 0) endStep = opts.endStep;
    if (!opts.outputOverride.empty()) cfg.outputFile = opts.outputOverride;

    // Validate step-range arguments
    if (startStep < 0 || startStep >= cfg.numSteps) {
      throw std::runtime_error("startStep is out of valid range.");
    }
    if (endStep < 0 || endStep >= cfg.numSteps) {
      throw std::runtime_error("endStep is out of valid range.");
    }
    if (endStep < startStep) {
      throw std::runtime_error("endStep must be >= startStep.");
    }

    // Validates visualization arguments
    if (opts.vizStep < 0 || opts.vizStep >= cfg.numSteps) {
      throw std::runtime_error("vizStep is out of valid range.");
    }
    if (opts.vizElectrons <= 0) {
      throw std::runtime_error("vizElectrons must be > 0.");
    }

    // Print the loaded configuration and active run options back to user
    std::cout << "Loaded config:\n"
              << "  temperature = " << cfg.temperature << " K\n"
              << "  pressure    = " << cfg.pressure << " Torr\n"
              << "  eField      = " << cfg.eField << " V/cm\n"
              << "  composition = [\"" << cfg.gas1 << "\", " << cfg.frac1
              << ", \"" << cfg.gas2 << "\", " << cfg.frac2 << "]\n"
              << "  gasFile     = " << cfg.gasFile << "\n"
              << "  iterations  = " << cfg.iterations << "\n"
              << "  minHeight   = " << cfg.minHeight << " cm\n"
              << "  numSteps    = " << cfg.numSteps << "\n"
              << "  stepSize    = " << cfg.stepSize << " cm\n"
              << "  outputFile  = " << cfg.outputFile << "\n"
              << "  startStep   = " << startStep << "\n"
              << "  endStep     = " << endStep << "\n"
              << "  visualize   = " << (opts.visualize ? "true" : "false") << "\n"
              << "  vizStep     = " << opts.vizStep << "\n"
              << "  vizElectrons= " << opts.vizElectrons << "\n";

    // Build or load Garfield gas medium
    Garfield::MediumMagboltz gas;
    BuildOrLoadGas(gas, cfg);

    // Build a simple box-shaped gas volume
    Garfield::GeometrySimple geo;
    Garfield::SolidBox box(0.0, 6.0, 0.0, 2.0, 6.0, 2.0);
    geo.AddSolid(&box, &gas);

    // Defines a constant electric field along +y. Thus electrons drift in -y direction
    Garfield::ComponentConstant comp;
    comp.SetGeometry(&geo);
    comp.SetElectricField(0.0, cfg.eField, 0.0);

    // Creates a sensor and attaches the field conponent. 
    // The sensor defines the active region in which Garfield tracks the electrons.
    Garfield::Sensor sensor;
    sensor.AddComponent(&comp);
    sensor.SetArea(-2.0, 0.0, -2.0, 2.0, 12.0, 2.0);

    // Create the drift transport object.
    // AvalancheMC is being used here mainly as a Monte Carlo drift engine.
    Garfield::AvalancheMC aval;
    aval.SetSensor(&sensor);
    aval.SetCollisionSteps(100);

    // Optional Visualization objects
    TApplication* app = nullptr;
    TCanvas* canvas = nullptr;
    Garfield::ViewDrift driftView;

    // If visualization requested, initializes the ROOT GUI and connect... 
    // ...Garfield's drift line viewer to a canvas
    if (opts.visualize) {
      int fakeArgc = 1;
      char appName[] = "GarfieldDriftVis";
      char* fakeArgv[] = {appName, nullptr};

      app = new TApplication("GarfieldDriftVis", &fakeArgc, fakeArgv);
      app->SetReturnFromRun(true);

      canvas = new TCanvas("cDrift", "TeBATDrift Garfield++ Visualization", 900, 700);

      driftView.SetCanvas(canvas);
      driftView.SetArea(-1.0, 0.0, -1.0, 1.0, 12.0, 1.0);

      aval.EnablePlotting(&driftView);
    }

    // Prepares the summary output file and checks directories exist
    EnsureParentDirExists(cfg.outputFile);
    std::ofstream out(cfg.outputFile.c_str());
    if (!out) {
      throw std::runtime_error("Could not open output file: " + cfg.outputFile);
    }

    out << std::setprecision(10);

    std::cout << "\nStarting drift scan...\n";

    // Loops over the requested drift-height steps.
    // For each height, we drift many electrons and then compute:
    //  - Mean drift time
    //  - Sigma_x
    //  - Sigma_z
    //  - Sigma_t
    for (int j = startStep; j <= endStep; ++j) {
      const double height = cfg.minHeight + cfg.stepSize * static_cast<double>(j);
      std::cout << "  Drifting electrons from height = " << height << " cm"
                << " (step " << j << ")\n";

      std::vector<double> xEnd;
      std::vector<double> zEnd;
      std::vector<double> tEnd;
      xEnd.reserve(cfg.iterations);
      zEnd.reserve(cfg.iterations);
      tEnd.reserve(cfg.iterations);

      const bool doVizThisStep = opts.visualize && (j == opts.vizStep);

      // Loops through electrons for this height.
      // Iterations = number of electrons simulated at this step.
      // Each electron is launched indenpendently from x=0, y=height, z=0, t=0.
      for (int i = 0; i < cfg.iterations; ++i) {
        if (doVizThisStep && i == opts.vizElectrons) {
          aval.DisablePlotting();
        }

	// Launch & drift the one electron
        aval.DriftElectron(0.0, height, 0.0, 0.0); // initial x, y, z, t

        double x0 = 0.0, y0 = 0.0, z0 = 0.0, t0 = 0.0;
        double x1 = 0.0, y1 = 0.0, z1 = 0.0, t1 = 0.0;
        int status = 0;

	// Retreives the endpoint information for electron in this drift sequence.
	// x1, y1, z1, t1 are the final position and time after drifting.
        aval.GetElectronEndpoint(0, x0, y0, z0, t0, x1, y1, z1, t1, status);

	// Store endpoints for later statistics
        xEnd.push_back(x1);
        zEnd.push_back(z1);
        tEnd.push_back(t1);
      }

      if (doVizThisStep && opts.visualize) {
        aval.EnablePlotting(&driftView);
      }

      // Compute statistsics from the raw endpoints
      const double meanT  = Mean(tEnd);
      const double meanX  = Mean(xEnd);
      const double meanZ  = Mean(zEnd);

      const double sigmaX = Sigma(xEnd, meanX);
      const double sigmaZ = Sigma(zEnd, meanZ);
      const double sigmaT = Sigma(tEnd, meanT);

      // Writes one summary row for this drift height.
      out << height << " "
          << meanT << " "
          << sigmaX << " "
          << sigmaZ << " "
          << sigmaT << "\n";
    }

    // Closes the summary file once all steps are complete.
    out.close();
    std::cout << "\nFinished. Summary written to: " << cfg.outputFile << "\n";

    // Displays the visualization if enabled.
    if (opts.visualize) {
      canvas->cd();
      driftView.Plot2d();

      canvas->Modified();
      canvas->Update();

      std::cout << "\nVisualization window opened.\n"
                << "Close the canvas window or quit the ROOT GUI to continue.\n";
      app->Run(true);
    }

    // If successful, return success.
    return 0;
  } catch (const std::exception& e) {
    // Catch any runtime/config/file errors and prints a readable message.
    std::cerr << "\nERROR: " << e.what() << "\n";
    return 1;
  }
}
