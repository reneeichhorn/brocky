#ifndef _QUIC_CLIENT_H_
#define _QUIC_CLIENT_H_

#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

/// Max buffer length for sending and receiving.
#define BUFFER_LEN 65535
/// Decides how big raw udp packages are
#define MAX_DATAGRAM_SIZE 1350
#define LOCAL_CONN_ID_LEN 16

#include <quiche.h>

class QUICClient {
  private:
    /// QUICHE Config object.
    quiche_config* pConfig = nullptr;
    quiche_conn* pQuicheRef = nullptr;

    // Buffers
    char pBuffer[BUFFER_LEN];
    uint8_t pSendBuffer[MAX_DATAGRAM_SIZE];

    // Socket
    int socketRef;
    struct addrinfo *pPeer;

  public:
    bool initialize();
    void tick();
    void cleanup();

    ~QUICClient() { this->cleanup(); }
};

#endif
