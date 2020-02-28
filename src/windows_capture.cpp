#include <cstdio>
#include <chrono>

#include "windows_capture.h"

void WindowsCapturer::cleanup() {
  // Clean encoder
  if (pEncoder) {
    pEncoder->DestroyEncoder();
    delete pEncoder;
    pEncoder = nullptr;
  }

  // Clean capturing
  SAFE_RELEASE(pDDA);
  SAFE_RELEASE(pDevice);
  SAFE_RELEASE(pContext);
  SAFE_RELEASE(pResource);
}

bool WindowsCapturer::initialize() {
  HRESULT hr = S_OK;

  // Temporary interfaces.
  IDXGIAdapter *pAdapter = nullptr;
  IDXGIDevice2* pDXGIDevice= nullptr;
  IDXGIOutput * pOutput = nullptr;
  IDXGIOutput1* pOut1 = nullptr;

  /// Release all temporary refs before exit
  #define CLEAN_EXIT() \
    SAFE_RELEASE(pDDA);\
    SAFE_RELEASE(pDevice);\
    SAFE_RELEASE(pContext);\
    SAFE_RELEASE(pResource);\
    SAFE_RELEASE(pAdapter);\
    SAFE_RELEASE(pDXGIDevice);\
    SAFE_RELEASE(pOutput);\
    SAFE_RELEASE(pOut1);\
    return false;

  // Initialize d3d device.
  D3D_FEATURE_LEVEL featureLevels[] =
  {
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_1
  };
  D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

  hr = D3D11CreateDevice(
    nullptr,
    D3D_DRIVER_TYPE_HARDWARE,
    nullptr,
    0,
    featureLevels,
    4,
    D3D11_SDK_VERSION,
    &this->pDevice,
    &featureLevel,
    &this->pContext
  );

  if (FAILED(hr)) {
    printf("Failed to create d3d11 device\n");
    CLEAN_EXIT();
  }

  // Query for dxgi interface. 
  hr = this->pDevice->QueryInterface(
    __uuidof(IDXGIDevice2), (void**)&pDXGIDevice
  );
  if (FAILED(hr)) {
    printf("Failed to query for dxgi interface\n");
    CLEAN_EXIT();
  }

  // Get dxgi adapter from dxgi device.
  hr = pDXGIDevice->GetParent(
    __uuidof(IDXGIAdapter),
    (void**)&pAdapter
  );
  if (FAILED(hr)) {
    printf("Failed to get adapter from dxgi device\n");
    CLEAN_EXIT();
  }

  // Get output device (for now always the main monitor 0)
  hr = pAdapter->EnumOutputs(0, &pOutput);
  if (FAILED(hr)) {
    printf("Failed to get output device 0 from dxgi adapter\n");
    CLEAN_EXIT();
  }

  // Grab interface for requested monitor output.
  hr = pOutput->QueryInterface(
    __uuidof(IDXGIOutput1),
    (void**)&pOut1
  );
  if (FAILED(hr)) {
    printf("Failed to query output from dxgi output\n");
    CLEAN_EXIT();
  }

  // Finally create output duplication
  hr = pOut1->DuplicateOutput(pDXGIDevice, &pDDA);

  // Get capture device meta data (size,..)
  DXGI_OUTDUPL_DESC outDesc;
  ZeroMemory(&outDesc, sizeof(outDesc));
  pDDA->GetDesc(&outDesc);
  height = outDesc.ModeDesc.Height;
  width = outDesc.ModeDesc.Width;
  printf("Capture device is ready with (w: %d; h: %d)\n", width, height);

  // Initialize Nvidia encoder.
  this->pEncoder = new NvEncoderD3D11(
    pDevice,
    width,
    height,
    NV_ENC_BUFFER_FORMAT_ARGB
  );

  if (!pEncoder) {
    printf("Failed to create nvidia encoder\n");
    CLEAN_EXIT();
  }

  ZeroMemory(&encInitParams, sizeof(encInitParams));
  ZeroMemory(&encConfig, sizeof(encConfig));
  ZeroMemory(&picParams, sizeof(picParams));

  // Basic init parameters.
  encInitParams.encodeConfig = &encConfig;  
  encInitParams.encodeWidth = width;
  encInitParams.encodeHeight = height;
  encInitParams.maxEncodeWidth = width;
  encInitParams.maxEncodeHeight = height;
  encConfig.gopLength = 5;

  // Picture encode parameters.
  picParams.codecPicParams.h264PicParams.forceIntraRefreshWithFrameCnt = false;
  picParams.codecPicParams.h264PicParams.sliceMode = 1;
  picParams.codecPicParams.h264PicParams.sliceModeData = 1500 - 28;
  picParams.codecPicParams.h264PicParams.constrainedFrame = 1;


  // Create encoder.
  try {
    // Grab some default values from presets.
    pEncoder->CreateDefaultEncoderParams(&encInitParams, NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_LOW_LATENCY_HQ_GUID);//NV_ENC_PRESET_LOW_LATENCY_HP_GUID);

    // Overrides of preset.
    // Important to make sure encoder is already putting it into
    // the slices that are ready for udp.
    encConfig.encodeCodecConfig.h264Config.sliceMode = 1;
    encConfig.encodeCodecConfig.h264Config.sliceModeData = 1500 - 28;
    encConfig.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
    encInitParams.frameRateNum = 60;
    encInitParams.reportSliceOffsets = 1;
    encInitParams.enableEncodeAsync = 0;

    // Create encoder.
    pEncoder->CreateEncoder(&encInitParams);
  } catch (...) {
    printf("Error thrown while trying to create default encoder.");
    CLEAN_EXIT();
  }
  
  // Release everything not needed anymore.
  SAFE_RELEASE(pAdapter);
  SAFE_RELEASE(pDXGIDevice);
  SAFE_RELEASE(pOutput);
  SAFE_RELEASE(pOut1);
  return true;
}

std::vector<std::vector<uint8_t>>* WindowsCapturer::captureFrame() {
  // Start measuring execution time.
  auto startTime = std::chrono::high_resolution_clock::now();

  ID3D11Texture2D* pFrameTexture = nullptr;
  HRESULT hr = S_OK;
  DXGI_OUTDUPL_FRAME_INFO frameInfo;

  // Create new raw frame info object.
  ZeroMemory(&frameInfo, sizeof(frameInfo));
  int acquired = 0;

  // Release all resources from previous frame to allow dxgi to optimize.
  if (pResource) {
    pDDA ->ReleaseFrame();
    pResource->Release();
    pResource = nullptr;
  }

  // Take next frame
  hr = pDDA->AcquireNextFrame(INFINITE, &frameInfo, &pResource);
  if (FAILED(hr) && hr != DXGI_ERROR_WAIT_TIMEOUT) {
    printf("Failed to capture next frame.. (error code: %X)\n", hr);
    return nullptr;
  }
  auto captureTimeEnd = std::chrono::high_resolution_clock::now();

  // No updates neeeded.
  if (frameInfo.AccumulatedFrames == 0 || frameInfo.LastPresentTime.QuadPart == 0) {
    statsSkipped++;
    return nullptr;
  }
  
  // can this happen??
  if (!pResource) {
    printf("Unexpected error, output resource is still empty\n");
    return nullptr;
  }

  // Query for D3DTexture resource.
  hr = pResource->QueryInterface(
    __uuidof(ID3D11Texture2D),
    (void**)&pFrameTexture
  );
  if (FAILED(hr)) {
    printf("Failed to get d3d texture from dxgi resource\n");
    return nullptr;
  }

  // Request new frame from encoder and copy over captured content.
  const NvEncInputFrame* pEncoderInputFrame = pEncoder->GetNextInputFrame();
  ID3D11Texture2D* pEncoderInputTexture = (ID3D11Texture2D *)pEncoderInputFrame->inputPtr;
  pContext->CopySubresourceRegion(
    pEncoderInputTexture,
    D3D11CalcSubresource(0, 0, 1),
    0,
    0,
    0,
    pFrameTexture,
    0,
    NULL
  );
  SAFE_RELEASE(pFrameTexture);
  pEncoderInputTexture->AddRef(); // ???

  // Start encoding.
  try {
    pEncoder->EncodeFrame(localEncodedBuffer, &picParams);
  } catch (...) {
    printf("Failed to encode frame with nvenc\n");
  }

  SAFE_RELEASE(pEncoderInputTexture);

  // Add some stats measurement
  auto endTime = std::chrono::high_resolution_clock::now();
  auto time = endTime - startTime;
  statsFrame++;
  statsExecutionTime += time / std::chrono::milliseconds(1);
  statsCaptureTime += (captureTimeEnd - startTime) / std::chrono::milliseconds(1);

  DWORD total = 0;
  for (std::vector<uint8_t> &packet : localEncodedBuffer) {
    total += packet.size();
  }
  statsTotal += total;
  statsPackets += localEncodedBuffer.size();
  
  return &localEncodedBuffer;
}

void WindowsCapturer::debugLastFrame() {
  DWORD total = 0;
  for (std::vector<uint8_t> &packet : localEncodedBuffer) {
    total += packet.size();
  }
  DWORD average = total == 0 ? 0 : total / localEncodedBuffer.size();

  printf("\n\n---------------------------------------------\n");
  printf("Encoded buffer informations:\n");
  printf("  Total: %d\n", total);
  printf("  Packets: %d\n", localEncodedBuffer.size());
  printf("  Avg: %d\n", average);
  printf("---------------------------------------------\n");
}

void WindowsCapturer::debugSession() {
  long long captureDiff = statsExecutionTime - statsCaptureTime;

  printf("\n\n---------------------------------------------\n");
  printf("Session stats:\n");
  printf("  Frames: %d (%d/s)\n", statsFrame, statsFrame / (statsExecutionTime / 1000));
  printf("  Total: %d KB\n", statsTotal / 1024);
  printf("  Total (avg): %d KB\n", statsTotal / statsFrame / 1024);
  printf("  Total (per Packet): %d KB\n", statsTotal / statsPackets / 1024);
  printf("  Total Packets: %d (Avg: %f)\n", statsPackets, (float)statsPackets / (float)statsFrame);
  printf("  Skipped Frames: %d\n", statsSkipped);
  printf("  Execution time: %llds (Avg: %lldms)\n", statsExecutionTime / 1000, statsExecutionTime / (long long)statsFrame);
  printf("  Capture time: %llds (Avg: %lldms)\n", statsCaptureTime / 1000, statsCaptureTime / (long long)statsFrame);
  printf("  Diff time: %llds (Avg: %lldms)\n", captureDiff / 1000, captureDiff / (long long)statsFrame);
  printf("---------------------------------------------\n");

  statsFrame = 0;
  statsSkipped = 0;
  statsExecutionTime = 0;
  statsTotal = 0;
  statsPackets = 0;
  statsCaptureTime = 0;
}