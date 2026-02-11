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
  if (bitsPerChannel <= 0) return 0;

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
 * Process pipeline:
 * 1) Read original 512x512 RGB
 * 2) Resample with 3x3 filter when S != 1.0
 * 3) Quantize (only uniform for now, M=-1)
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
  outW = static_cast<int>(std::lround(inW * S));
  outH = static_cast<int>(std::lround(inH * S));

  cout << "Input:  " << inW << "x" << inH << "\n";
  cout << "Output: " << outW << "x" << outH << "  (S=" << S << ", Q=" << Q << ", M=" << M << ", E=" << E << ")\n";


  if (outW < 1) outW = 1;
  if (outH < 1) outH = 1;

  // Allocate output buffer (must be malloc)
  unsigned char* outData =
      (unsigned char*)malloc(outW * outH * 3 * sizeof(unsigned char));

  // Bits per channel (for now we assume equal split; extra credit comes later)
  int bitsPerChannel = (E == 0) ? (Q / 3) : (Q / 3);

  // Resample: for each output pixel, map back to input coordinate, filter, then quantize
  for (int y = 0; y < outH; y++) {
    for (int x = 0; x < outW; x++) {
      // Map output coordinate to input coordinate
      float xf = (S == 0.0f) ? 0.0f : (x / S);
      float yf = (S == 0.0f) ? 0.0f : (y / S);

      int cx = static_cast<int>(std::lround(xf));
      int cy = static_cast<int>(std::lround(yf));
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

        } else if (M > 0 && M < 255) {
            // Logarithmic quantization around pivot M

            int L = 1 << bitsPerChannel;
            double pivot = (double)M;
            double normalized;

            if (filtered < pivot) {
                normalized = log(1 + (pivot - filtered)) / log(1 + pivot);
                normalized = 1.0 - normalized;
            } else {
                normalized = log(1 + (filtered - pivot)) / log(1 + (255 - pivot));
                normalized = 1.0 + normalized;
            }

            int level = (int)(normalized * (L / 2));
            level = clampInt(level, 0, L - 1);

            int reconstructed = (int)((double)level / (L - 1) * 255.0);
            qv = clampInt(reconstructed, 0, 255);

        } else if (M == 256) {
            // For now, use uniform (we'll replace with optimal next)
            qv = quantizeUniform(filtered, bitsPerChannel);
        }


        outData[(y * outW + x) * 3 + c] = qv;
      }
    }
  }

  // Analysis metrics (only meaningful when S=1.0 and you have a quantized output)
  if (S == 1.0f && M == -1) {
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
  float S = std::stof(wxApp::argv[2].ToStdString());
  int Q = std::stoi(wxApp::argv[3].ToStdString());
  int M = std::stoi(wxApp::argv[4].ToStdString());
  int E = (wxApp::argc == 6) ? std::stoi(wxApp::argv[5].ToStdString()) : 0;

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
