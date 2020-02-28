#ifndef _WINDOWS_CAPTURE_H_
#define _WINDOWS_CAPTURE_H_

// DXGI and D3D API
#include <dxgi1_2.h>
#include <d3d11_2.h>

// Nvidia encoder api
#include "NvEncoder/NvEncoderD3D11.h"

class WindowsCapturer {
  private:
    /// The DDA object
    IDXGIOutputDuplication* pDDA = nullptr;
    /// The D3D11 device used by the DDA session
    ID3D11Device* pDevice = nullptr;
    /// The D3D11 Device Context used by the DDA session
    ID3D11DeviceContext* pContext = nullptr;
    /// The resource used to acquire a new captured frame from DDA
    IDXGIResource *pResource = nullptr;
    /// Output width obtained from DXGI_OUTDUPL_DESC
    DWORD width = 0;
    /// Output height obtained from DXGI_OUTDUPL_DESC
    DWORD height = 0;
    /// Debug stats.
    DWORD statsFrame = 0;
    DWORD statsSkipped = 0;
    long long statsExecutionTime = 0;
    long long statsCaptureTime = 0;
    long long statsPackets = 0;
    long long statsTotal = 0;

    /// NVENCODE API wrapper. Defined in NvEncoderD3D11.h. This class is imported from NVIDIA Video SDK
    NvEncoderD3D11 *pEncoder = nullptr;
    /// NVENCODEAPI session intialization parameters
    NV_ENC_INITIALIZE_PARAMS encInitParams = { 0 };
    /// NVENCODEAPI video encoding configuration parameters
    NV_ENC_CONFIG encConfig = { 0 };
    /// NVENCODEAPI paramters for encoding command.
    NV_ENC_PIC_PARAMS picParams = { 0 };
    /// Encoded video bitstream packet in CPU memory
    std::vector<std::vector<uint8_t>> localEncodedBuffer;

  public:
    ~WindowsCapturer() { this->cleanup(); }

    bool initialize();
    void cleanup();
    
    std::vector<std::vector<uint8_t>>* captureFrame();
    void debugLastFrame();
    void debugSession();
};

// Macro to release and null a dxgi resource.
#if !defined(SAFE_RELEASE)
#define SAFE_RELEASE(X) if(X){X->Release(); X=nullptr;}
#endif

#endif