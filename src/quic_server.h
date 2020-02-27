#ifndef _QUIC_SERVER_H_
#define _QUIC_SERVER_H_

#include <vector>
#include <map>
#include <sstream>
#include <iomanip>

#include <winsock2.h>

#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif
#include <quiche.h>

/// Max buffer length for sending and receiving.
#define BUFFER_LEN 65535

/// Decides how big raw udp packages are
#define MAX_DATAGRAM_SIZE 1350

struct ClientRef {
  uint8_t dcid[QUICHE_MAX_CONN_ID_LEN];
  quiche_conn* quiche_ref;
  struct sockaddr addr;
};

class QUICServer {
  private:
    /// Winsock reference
    WSADATA pWSA;
    SOCKET pServerSocket;

    // Buffers
    char pBuffer[BUFFER_LEN];
    uint8_t pSendBuffer[MAX_DATAGRAM_SIZE];

    /// QUICHE Config object.
    quiche_config* pConfig = nullptr;

    // Client references.
    std::map<std::string, ClientRef> clientRefs;

  public:
    ~QUICServer() { this->cleanup(); }

    bool initialize();
    void tick(std::vector<std::vector<uint8_t>>* frameData);
    void cleanup();

  private:
    void negotiateVersion(uint32_t peerVersion);
    void createToken(
      const uint8_t *scid, size_t scid_len,
      const uint8_t *dcid, size_t dcid_len,
      struct sockaddr_in *addr, int addr_len,
      uint8_t *token, size_t *token_len
    );
};

#define LOCAL_CONN_ID_LEN 16

#define MAX_TOKEN_LEN \
    (sizeof("quiche") - 1 + \
    sizeof(struct sockaddr_storage) + \
    QUICHE_MAX_CONN_ID_LEN)


#endif
