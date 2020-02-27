#include <cstdio>

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
      printf("Windows capturer initialized without errors..\n");

      for (int i = 0; i < 500; i++) {
        capturer->captureFrame();
        server->tick(nullptr);
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

void main () {
  win_server_main();
}