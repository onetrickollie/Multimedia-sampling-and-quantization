## Personal Information
- Name: Kaixiang Liu
- USCID: 9777953877
- Date: Ferbruary 13, 2026

## Project Overview
This project implements image resampling and three quantization methods (Uniform, Logarithmic, and Optimal). 

## Prerequisites

### macOS
- Xcode Command Line Tools
- CMake (version 3.10 or higher)
- wxWidgets (installed via Homebrew)

Install prerequisites:
```bash
xcode-select --install
brew install cmake
brew install wxwidgets
```

### Linux (Ubuntu/Debian)
- GCC/G++ compiler
- CMake (version 3.10 or higher)
- wxWidgets development libraries
- X11 development libraries

Install prerequisites:
```bash
sudo apt-get update
sudo apt-get install build-essential cmake
sudo apt-get install libwxgtk3.0-gtk3-dev
sudo apt-get install libx11-dev
```

### Windows
- Visual Studio 2019 or later (with C++ desktop development)
- CMake (version 3.10 or higher)
- wxWidgets is bundled in the dependency folder

## Compilation Instructions

### On macOS or Linux

1. Navigate to the project directory:
```bash
cd ImageDisplay_C++_cross_platform
```

2. Create and enter the build directory:
```bash
mkdir -p build
cd build
```

3. Generate build files with CMake:
```bash
cmake ..
```

4. Compile the project:
```bash
make
```

The executable `MyImageApplication` will be created in the `build/` directory.

### On Windows

1. Open Command Prompt or PowerShell

2. Navigate to the project directory:
```cmd
cd ImageDisplay_C++_cross_platform
```

3. Create and enter the build directory:
```cmd
mkdir build
cd build
```

4. Generate Visual Studio solution:
```cmd
cmake ..
```

5. Build the project:
```cmd
cmake --build . --config Release
```

Alternatively, you can open the generated `.sln` file in Visual Studio and build from there.

The executable will be in `build/Release/MyImageApplication.exe`

## Running the Program

### Command Line Syntax
```
./MyImageApplication <image.rgb> <S> <Q> <M> [E]
```

### Parameters
- `<image.rgb>` : Input image file path (must be 512×512 RGB format)
- `<S>` : Scale factor (0.0 < S ≤ 1.0)
  - 1.0 = no scaling
  - 0.5 = half size (256×256)
  - 0.25 = quarter size (128×128)
- `<Q>` : Total quantization bits (1 ≤ Q ≤ 24, must be multiple of 3 if E=0)
  - Q/3 bits allocated to each channel (R, G, B)
  - Examples: 3, 6, 9, 12, 15, 18, 21, 24
- `<M>` : Quantization mode
  - -1 = Uniform quantization
  - 0-255 = Logarithmic quantization (pivot value)
  - 256 = Optimal quantization (Lloyd-Max algorithm)
- `[E]` : Extra credit flag (optional)
  - 0 = Equal bits per channel (default)
  - 1 = Optimal bit distribution across channels


## Program Output

The program will:
1. Display the processed image in a wxWidgets window
2. Print to console:
   - Input and output dimensions
   - Parameter values used (S, Q, M, E)
   - MSE and MAE values (when S=1.0 only)

Example console output:
```
Number of command line arguments: 5
Input:  512x512
Output: 512x512  (S=1, Q=9, M=-1, E=0)
MSE(sum) = 69839940
MAE(sum) = 6425140
```


## Known Limitations

1. Input images must be exactly 512×512 pixels in .rgb format
2. MSE/MAE metrics only calculated when S=1.0 (no scaling)
3. When E=0, Q must be a multiple of 3 for equal channel distribution

## Testing

To run comprehensive tests, use the following Q values with different modes:

```bash
# Test uniform quantization
for Q in 3 6 9 12 15 18 21 24; do
  ./MyImageApplication ../../Lena_512_512.rgb 1.0 $Q -1
done

# Test logarithmic quantization (M=128)
for Q in 3 6 9 12 15 18 21 24; do
  ./MyImageApplication ../../Lena_512_512.rgb 1.0 $Q 128
done

# Test optimal quantization
for Q in 3 6 9 12 15 18 21 24; do
  ./MyImageApplication ../../Lena_512_512.rgb 1.0 $Q 256
done
```

## Analysis

Error analysis graphs and detailed written analysis are provided in the separate analysis document (CS576Analysis.pdf).

## Contact

For questions or issues, please contact [liukaixi@usc.edu]