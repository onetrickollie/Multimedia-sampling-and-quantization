#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cmath>
#include <cstdlib>

using namespace std;
namespace fs = std::filesystem;

/**
 * Display an image using WxWidgets.
 * https://www.wxwidgets.org/
 */

/** Declarations*/

/**
 * Class that implements wxApp
 */
class MyApp : public wxApp {
 public:
  bool OnInit() override;
};

/**
 * Class that implements wxFrame.
 * This frame serves as the top level window for the program
 */
class MyFrame : public wxFrame {
 public:
  MyFrame(const wxString &title, string imagePath, float S, int Q, int M, int E);

 private:
  void OnPaint(wxPaintEvent &event);
  wxImage inImage;
  wxScrolledWindow *scrolledWindow;
  int width;
  int height;

  // added SQME arguments
  float scale;
  int qBits;
  int mode;
  int extraCredit;
};

/** Utility function to read image data */
unsigned char *readImageData(string imagePath, int width, int height);

/** Helper clamp */
static inline int clampInt(int v, int lo, int hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

/**
 * Convert input value (0..255) to quantized representative value (0..255)
 * Uniform quantization (M = -1)
 */
static inline unsigned char quantizeUniform(unsigned char v, int bitsPerChannel) {
  if (bitsPerChannel >= 8) return v;
  if (bitsPerChannel <= 0) bitsPerChannel = 1;

  int L = 1 << bitsPerChannel; // number of levels
  int step = 256 / L;          // interval size in [0..255]
  int k = v / step;            // interval index
  if (k >= L) k = L - 1;

  // representative is the center of the interval
  int rep = k * step + step / 2;
  rep = clampInt(rep, 0, 255);
  return static_cast<unsigned char>(rep);
}

/**
 * 3x3 average filter at (cx, cy), ignoring out-of-range pixels
 * channel c: 0=R,1=G,2=B
 */
static inline unsigned char avg3x3(const unsigned char* inData, int inW, int inH,
                                   int cx, int cy, int c) {
  int sum = 0;
  int cnt = 0;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      int x = cx + dx;
      int y = cy + dy;
      if (x < 0 || x >= inW || y < 0 || y >= inH) continue;
      int idx = (y * inW + x) * 3 + c;
      sum += inData[idx];
      cnt++;
    }
  }
  if (cnt == 0) return 0;
  int val = (sum + cnt / 2) / cnt; // rounded average
  val = clampInt(val, 0, 255);
  return static_cast<unsigned char>(val);
}

/**
 * Optimal interval quantization helpers (M=256)
 * We'll use a simple Lloyd-Max style update for representative levels.
 */
static inline unsigned char quantizeOptimal(unsigned char v,
                                            const vector<int>& representatives) {
  int bestIdx = 0;
  int bestDist = abs((int)v - representatives[0]);

  for (int i = 1; i < (int)representatives.size(); i++) {
    int dist = abs((int)v - representatives[i]);
    if (dist < bestDist) {
      bestDist = dist;
      bestIdx = i;
    }
  }
  return (unsigned char)representatives[bestIdx];
}

static inline vector<int> buildOptimalLevels(const unsigned char* data,
                                             int totalPixels,
                                             int bitsPerChannel,
                                             int channelOffset) {
  int L = 1 << bitsPerChannel;

  // Initialize centers uniformly
  vector<double> centers(L);
  for (int i = 0; i < L; i++) {
    centers[i] = (double)i / (L - 1) * 255.0;
  }

  // Iterate Lloyd algorithm (fixed iterations)
  for (int iter = 0; iter < 10; iter++) {
    vector<double> sum(L, 0.0);
    vector<int> count(L, 0);

    for (int i = 0; i < totalPixels; i++) {
      int value = (int)data[i * 3 + channelOffset];

      // Find closest center
      int bestIdx = 0;
      double bestDist = fabs(value - centers[0]);
      for (int k = 1; k < L; k++) {
        double dist = fabs(value - centers[k]);
        if (dist < bestDist) {
          bestDist = dist;
          bestIdx = k;
        }
      }

      sum[bestIdx] += value;
      count[bestIdx]++;
    }

    // Update centers
    for (int k = 0; k < L; k++) {
      if (count[k] > 0) centers[k] = sum[k] / count[k];
    }
  }

  // Convert to integer representatives
  vector<int> reps(L);
  for (int i = 0; i < L; i++) {
    reps[i] = clampInt((int)lround(centers[i]), 0, 255);
  }
  return reps;
}

/**
 * FIXED: Logarithmic interval quantization (M in 0..255, pivot point)
 * 
 * Strategy: Create L bins total where bins near M are smaller (more precision)
 * and bins far from M are larger (less precision).
 * 
 * We use exponential spacing: bins grow exponentially as we move away from M.
 */
/**
 * FIXED: Logarithmic interval quantization
 * Create L bins with exponential spacing around pivot M.
 * Bins near M are smaller (higher precision), bins far from M are larger.
 */
static inline void buildLogBins(int M, int L, vector<int>& edges, vector<int>& reps) {
  edges.clear();
  reps.clear();
  
  if (L <= 0) return;
  
  // Special case: only 1 level
  if (L == 1) {
    edges = {0, 256};
    reps = {128};
    return;
  }
  
  // Allocate bins proportionally to range size
  int leftRange = M - 0;        // [0, M]
  int rightRange = 255 - M;     // [M, 255]
  
  // Number of bins on each side
  int leftBins = (int)round((double)L * leftRange / 256.0);
  int rightBins = L - leftBins;
  
  // Edge cases: ensure at least some bins on each side if M is not at boundary
  if (M > 0 && leftBins == 0) {
    leftBins = 1;
    rightBins = L - 1;
  }
  if (M < 255 && rightBins == 0) {
    rightBins = 1;
    leftBins = L - 1;
  }
  
  // Generate exponential spacing parameter
  double k = 2.0; // controls curvature
  
  vector<double> allEdges;
  allEdges.push_back(0.0);
  
  // LEFT SIDE [0, M]: Generate internal edges
  // We want leftBins bins on the left, which means leftBins-1 internal edges
  if (leftBins > 0) {
    for (int i = 1; i < leftBins; i++) {
      double t = (double)i / (double)leftBins;
      double tRev = 1.0 - t;
      double normalized = (exp(tRev * k) - 1.0) / (exp(k) - 1.0);
      double edgeVal = M * (1.0 - normalized);
      allEdges.push_back(edgeVal);
    }
  }
  
  // Add M as boundary (only if it's not at 0 or 255)
  if (M > 0 && M < 255) {
    allEdges.push_back((double)M);
  }
  
  // RIGHT SIDE [M, 255]: Generate internal edges
  // We want rightBins bins on the right, which means rightBins-1 internal edges
  if (rightBins > 0) {
    for (int i = 1; i < rightBins; i++) {
      double t = (double)i / (double)rightBins;
      double normalized = (exp(t * k) - 1.0) / (exp(k) - 1.0);
      double edgeVal = M + (255.0 - M) * normalized;
      allEdges.push_back(edgeVal);
    }
  }
  
  // Add final boundary at 256
  allEdges.push_back(256.0);
  
  // Convert to integers and ensure monotonic
  edges.clear();
  for (double e : allEdges) {
    edges.push_back(clampInt((int)round(e), 0, 256));
  }
  
  // Force monotonic and remove duplicates
  vector<int> cleanEdges;
  cleanEdges.push_back(edges[0]);
  for (int i = 1; i < (int)edges.size(); i++) {
    if (edges[i] > cleanEdges.back()) {
      cleanEdges.push_back(edges[i]);
    }
  }
  edges = cleanEdges;
  
  // If we don't have exactly L+1 edges, fallback to uniform
  if ((int)edges.size() != L + 1) {
    edges.clear();
    for (int i = 0; i <= L; i++) {
      edges.push_back(i * 256 / L);
    }
    edges.back() = 256;
  }
  
  // Compute representative values (bin centers)
  reps.resize(L);
  for (int i = 0; i < L; i++) {
    int a = edges[i];
    int b = edges[i + 1];
    int center = (a + b - 1) / 2;
    reps[i] = clampInt(center, 0, 255);
  }
}

static inline unsigned char quantizeLog(unsigned char v, int M, int bitsPerChannel) {
  int L = 1 << bitsPerChannel;

  vector<int> edges, reps;
  buildLogBins(M, L, edges, reps);

  int iv = (int)v;
  // Find interval i such that edges[i] <= iv < edges[i+1]
  int i = 0;
  for (; i < L; i++) {
    if (iv >= edges[i] && iv < edges[i + 1]) break;
  }
  if (i >= L) i = L - 1;
  return (unsigned char)reps[i];
}

/**
 * Process pipeline:
 * 1) Read original 512x512 RGB
 * 2) Resample with 3x3 filter when S != 1.0
 * 3) Quantize (uniform / log / optimal)
 *
 * Returns malloc() buffer for wxImage ownership.
 * Also returns outW/outH via references.
 */
unsigned char* processImage(const string& imagePath,
                            int inW, int inH,
                            float S, int Q, int M, int E,
                            int& outW, int& outH) {
  // Read original (unmodified) pixels in RGBRGB... layout
  unsigned char* original = readImageData(imagePath, inW, inH);

  // Compute output dimensions
  outW = static_cast<int>(lround(inW * S));
  outH = static_cast<int>(lround(inH * S));

  cout << "Input:  " << inW << "x" << inH << "\n";
  cout << "Output: " << outW << "x" << outH
       << "  (S=" << S << ", Q=" << Q << ", M=" << M << ", E=" << E << ")\n";

  if (outW < 1) outW = 1;
  if (outH < 1) outH = 1;

  // Allocate output buffer (must be malloc)
  unsigned char* outData =
      (unsigned char*)malloc(outW * outH * 3 * sizeof(unsigned char));

  // Bits per channel (for now we assume equal split; extra credit comes later)
  int bitsPerChannel = (E == 0) ? (Q / 3) : (Q / 3);

  // Precompute optimal representatives if needed (M=256)
  vector<int> optR, optG, optB;
  if (M == 256) {
    int totalPixels = inW * inH;
    optR = buildOptimalLevels(original, totalPixels, bitsPerChannel, 0);
    optG = buildOptimalLevels(original, totalPixels, bitsPerChannel, 1);
    optB = buildOptimalLevels(original, totalPixels, bitsPerChannel, 2);
  }

  // Resample: for each output pixel, map back to input coordinate, filter, then quantize
  for (int y = 0; y < outH; y++) {
    for (int x = 0; x < outW; x++) {
      // Map output coordinate to input coordinate
      float xf = (S == 0.0f) ? 0.0f : (x / S);
      float yf = (S == 0.0f) ? 0.0f : (y / S);

      int cx = (int)lround(xf);
      int cy = (int)lround(yf);
      cx = clampInt(cx, 0, inW - 1);
      cy = clampInt(cy, 0, inH - 1);

      for (int c = 0; c < 3; c++) {
        unsigned char filtered;

        if (S == 1.0f) {
          // No scaling: just copy original pixel
          filtered = original[(cy * inW + cx) * 3 + c];
        } else {
          // Scaling: use 3x3 average filter centered at mapped input pixel
          filtered = avg3x3(original, inW, inH, cx, cy, c);
        }

        unsigned char qv = filtered;

        if (M == -1) {
          // Uniform
          qv = quantizeUniform(filtered, bitsPerChannel);

        } else if (M >= 0 && M <= 255) {
          // Logarithmic quantization around pivot M
          qv = quantizeLog(filtered, M, bitsPerChannel);

        } else if (M == 256) {
          // Optimal interval quantization using Lloyd-style reps
          if (c == 0) qv = quantizeOptimal(filtered, optR);
          else if (c == 1) qv = quantizeOptimal(filtered, optG);
          else qv = quantizeOptimal(filtered, optB);
        }

        outData[(y * outW + x) * 3 + c] = qv;
      }
    }
  }

  // Analysis metrics (only meaningful when S=1.0 and output is same size)
  if (S == 1.0f && outW == inW && outH == inH) {
    long long mseSum = 0;
    long long maeSum = 0;

    for (int i = 0; i < inW * inH * 3; i++) {
      int diff = (int)original[i] - (int)outData[i];
      mseSum += 1LL * diff * diff;
      maeSum += llabs(diff);
    }

    cout << "MSE(sum) = " << mseSum << endl;
    cout << "MAE(sum) = " << maeSum << endl;
  }

  // wxImage will own outData, so we free original ourselves
  free(original);

  return outData;
}

/** Definitions */

/**
 * Init method for the app.
 * Here we process the command line arguments and
 * instantiate the frame.
 */
bool MyApp::OnInit() {
  wxInitAllImageHandlers();

  // deal with command line arguments here
  cout << "Number of command line arguments: " << wxApp::argc << endl;
  if (wxApp::argc != 5 && wxApp::argc != 6) {
    cerr << "Usage: " << wxApp::argv[0]
         << " <image.rgb> <S> <Q> <M> [E]\n"
         << "Example: " << wxApp::argv[0] << " Lena_512_512.rgb 0.75 9 0\n";
    exit(1);
  }

  string imagePath = wxApp::argv[1].ToStdString();
  float S = stof(wxApp::argv[2].ToStdString());
  int Q = stoi(wxApp::argv[3].ToStdString());
  int M = stoi(wxApp::argv[4].ToStdString());
  int E = (wxApp::argc == 6) ? stoi(wxApp::argv[5].ToStdString()) : 0;

  // basic validation
  if (!(S > 0.0f && S <= 1.0f)) { cerr << "Error: S must be in (0,1].\n"; exit(1); }
  if (!(Q >= 1 && Q <= 24)) { cerr << "Error: Q must be in [1,24].\n"; exit(1); }
  if (!((M == -1) || (M >= 0 && M <= 255) || (M == 256))) {
    cerr << "Error: M must be -1, 0..255, or 256.\n"; exit(1);
  }
  if (E != 0 && E != 1) { cerr << "Error: E must be 0 or 1.\n"; exit(1); }
  if (E == 0 && (Q % 3 != 0)) {
    cerr << "Error: Without extra credit (E=0), Q must be a multiple of 3.\n";
    exit(1);
  }

  MyFrame *frame = new MyFrame("Image Display", imagePath, S, Q, M, E);
  frame->Show(true);
  return true;
}

/**
 * Constructor for the MyFrame class.
 * Here we read the pixel data from the file and set up the scrollable window.
 */
MyFrame::MyFrame(const wxString &title, string imagePath, float S, int Q, int M, int E)
    : wxFrame(NULL, wxID_ANY, title), scale(S), qBits(Q), mode(M), extraCredit(E) {

  int inW = 512;
  int inH = 512;

  // Process image based on S/Q/M/E and produce output buffer owned by wxImage
  int outW = 0;
  int outH = 0;
  unsigned char *outData = processImage(imagePath, inW, inH, scale, qBits, mode, extraCredit, outW, outH);

  width = outW;
  height = outH;

  // the last argument is static_data, if it is false, after this call the
  // pointer to the data is owned by the wxImage object, which will be
  // responsible for deleting it. So this means that you should not delete the
  // data yourself.
  inImage.SetData(outData, width, height, false);

  // Set up the scrolled window as a child of this frame
  scrolledWindow = new wxScrolledWindow(this, wxID_ANY);
  scrolledWindow->SetScrollbars(10, 10, width, height);
  scrolledWindow->SetVirtualSize(width, height);

  // Bind the paint event to the OnPaint function of the scrolled window
  scrolledWindow->Bind(wxEVT_PAINT, &MyFrame::OnPaint, this);

  // Set the frame size
  SetClientSize(width, height);

  // Set the frame background color
  SetBackgroundColour(*wxBLACK);
}

/**
 * The OnPaint handler that paints the UI.
 * Here we paint the image pixels into the scrollable window.
 */
void MyFrame::OnPaint(wxPaintEvent &event) {
  wxBufferedPaintDC dc(scrolledWindow);
  scrolledWindow->DoPrepareDC(dc);

  wxBitmap inImageBitmap = wxBitmap(inImage);
  dc.DrawBitmap(inImageBitmap, 0, 0, false);
}

/** Utility function to read image data */
unsigned char *readImageData(string imagePath, int width, int height) {

  // Open the file in binary mode
  ifstream inputFile(imagePath, ios::binary);

  if (!inputFile.is_open()) {
    cerr << "Error Opening File for Reading" << endl;
    exit(1);
  }

  // Create and populate RGB buffers
  vector<char> Rbuf(width * height);
  vector<char> Gbuf(width * height);
  vector<char> Bbuf(width * height);

  /**
   * The input RGB file is formatted as RRRR.....GGGG....BBBB.
   * i.e the R values of all the pixels followed by the G values
   * of all the pixels followed by the B values of all pixels.
   * Hence we read the data in that order.
   */
  inputFile.read(Rbuf.data(), width * height);
  inputFile.read(Gbuf.data(), width * height);
  inputFile.read(Bbuf.data(), width * height);

  inputFile.close();

  /**
   * Allocate a buffer to store the pixel values
   * The data must be allocated with malloc(), NOT with operator new. wxWidgets
   * library requires this.
   */
  unsigned char *inData =
      (unsigned char *)malloc(width * height * 3 * sizeof(unsigned char));

  for (int i = 0; i < height * width; i++) {
    // We populate RGB values of each pixel in that order
    // RGB.RGB.RGB and so on for all pixels
    inData[3 * i] = (unsigned char)Rbuf[i];
    inData[3 * i + 1] = (unsigned char)Gbuf[i];
    inData[3 * i + 2] = (unsigned char)Bbuf[i];
  }

  return inData;
}

wxIMPLEMENT_APP(MyApp);