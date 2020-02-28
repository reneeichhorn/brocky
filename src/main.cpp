#include <cstdio>

#ifndef RPI_CLIENT
#include "quic_server.h"
#include "windows_capture.h"

// Entry point for windows main server.
void win_server_main () {
  WindowsCapturer* capturer = new WindowsCapturer();
  QUICServer* server = new QUICServer();

  printf("Initializing QUIC server\n");
  if (server->initialize()) {
    printf("Initializing windows capturing\n");
    if (capturer->initialize()) {
      printf("Windows capturer initialized without errors...\n");

      while (true) {
        auto encodedFrame = capturer->captureFrame();
        if (encodedFrame) {
          server->tick(encodedFrame);
        }
      }

      capturer->debugSession();
    }
  };

  printf("Exiting...\n");
  capturer->cleanup();
  server->cleanup();

  delete capturer;
  delete server;
}
#else
#include <chrono>
#include <thread>

#include "quic_client.h"

void rpi_client_main() {
  printf("Connecting to QUIC server..\n");
  QUICClient* client = new QUICClient();

  if (client->initialize()) {
    printf("Initializing VideoCore decoder..\n");

    printf("Start taking frames..\n");
    while (true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
      client->tick();
    }
  }

  printf("Exiting..\n");
  client->cleanup();
  delete client;
}
#endif

int main () {
  #ifndef RPI_CLIENT
  win_server_main();
  #else
  rpi_client_main();
  #endif

  return 0;
}