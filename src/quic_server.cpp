#include "quic_server.h"

void QUICServer::cleanup() {
  WSACleanup();

  if (pConfig) {
    quiche_config_free(pConfig);
    pConfig = nullptr;
  }
}

bool QUICServer::initialize() {
  // Initialize winsock
	if (WSAStartup(MAKEWORD(2,2), &pWSA) != 0) {
		printf("Could not load winsock2.2 (error code: %d)\n", WSAGetLastError());
    return false;
	}
  
  // Initialize server socket. 
  if((pServerSocket = socket(AF_INET , SOCK_DGRAM , 0 )) == INVALID_SOCKET) {
		printf("Could not create server socket (error code: %d)\n", WSAGetLastError());
    return false;
	}

  // Prepare socket setup.
  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(1337);

  // Bind to address.
	if(bind(pServerSocket ,(struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		printf("Failed to bind to port 1337 (error code: %d)\n", WSAGetLastError());
    return false;
	}

  // Set socket to non-blocking
  u_long mode = 1;
  if (ioctlsocket(pServerSocket, FIONBIO, &mode) != NO_ERROR) {
		printf("Failed to set socket to non-blocking (error code: %d)\n", WSAGetLastError());
    return false;
  }

  // Initialize quiche config
  pConfig = quiche_config_new(QUICHE_PROTOCOL_VERSION);
  if (!pConfig) {
    printf("Failed to create quiche configuration\n");
    return false;
  }

  // Apply configuration.
  quiche_config_set_max_packet_size(pConfig, MAX_DATAGRAM_SIZE);

  return true;
}

std::string hexStr(char *data, int len) {
  std::stringstream ss;
  ss << std::hex;

  for( int i(0) ; i < len; ++i )
      ss << std::setw(2) << std::setfill('0') << (int)data[i];

  return ss.str();
}

/// Generate a stateless retry token.
///
/// The token includes the static string `"quiche"` followed by the IP address
/// of the client and by the original destination connection ID generated by the
/// client.
///
/// Note that this function is only an example and doesn't do any cryptographic
/// authenticate of the token. *It should not be used in production system*.
static void mint_token(const uint8_t *dcid, size_t dcid_len,
                       struct sockaddr_in *addr, int addr_len,
                       uint8_t *token, size_t *token_len) {
  memcpy(token, "quiche", sizeof("quiche") - 1);
  memcpy(token + sizeof("quiche") - 1, addr, addr_len);
  memcpy(token + sizeof("quiche") - 1 + addr_len, dcid, dcid_len);
  *token_len = sizeof("quiche") - 1 + addr_len + dcid_len;
}

static bool validate_mint_token(const uint8_t *token, size_t token_len,
                           struct sockaddr_in *addr, int addr_len,
                           uint8_t *odcid, size_t *odcid_len) {
    if ((token_len < sizeof("quiche") - 1) ||
         memcmp(token, "quiche", sizeof("quiche") - 1)) {
        return false;
    }

    token += sizeof("quiche") - 1;
    token_len -= sizeof("quiche") - 1;

    if ((token_len < addr_len) || memcmp(token, addr, addr_len)) {
        return false;
    }

    token += addr_len;
    token_len -= addr_len;

    if (*odcid_len < token_len) {
        return false;
    }

    memcpy(odcid, token, token_len);
    *odcid_len = token_len;

    return true;
}

void QUICServer::tick(std::vector<std::vector<uint8_t>>* frameData) {
  // Send frame data to all active connections.
  for (auto iter = clientRefs.begin(); iter != clientRefs.end(); iter++) {
    // Only take clients that are ready.
    auto ref = iter->second.quiche_ref;
    auto isEstablished = quiche_conn_is_established(ref);
    auto isEarlyStage = quiche_conn_is_in_early_data(ref);
    if (!isEstablished && !isEarlyStage) {
      // Force quiche to create sliced QUIC packets.
      quiche_stream_iter *writeable = quiche_conn_writable(ref);
      uint64_t id = 0;

      while (quiche_stream_iter_next(writeable, &id)) {
        bool finish = false;
        size_t recv_len = quiche_conn_stream_send(ref, id, pSendBuffer, sizeof(pSendBuffer), &finish);
      }
      quiche_stream_iter_free(writeable);
    }

    // Get all outstanding QUIC packets and send them over.
    while (true) {
      size_t written = quiche_conn_send(ref, pSendBuffer, sizeof(pSendBuffer));
      if (written == QUICHE_ERR_DONE) {
        break;
      }

      // Send retry packet over udp.
      size_t sent = sendto(pServerSocket, (char*)pSendBuffer, written, 0,
                           &iter->second.addr,
                            sizeof(iter->second.addr));
      
      printf("[QUIC] Sending QUIC packet over UDP (size: %d; actual: %d)\n", written, sent);
    }
  }

  // Try to read raw udp data
  int recvLength = 0;
  struct sockaddr_in peer_addr;
  int peer_addr_len = sizeof(peer_addr);
  memset(&peer_addr, 0, peer_addr_len);

  if ((recvLength = recvfrom(pServerSocket, pBuffer, BUFFER_LEN, 0, (struct sockaddr *)&peer_addr, &peer_addr_len)) == SOCKET_ERROR) {
    auto error = WSAGetLastError();

    // Since we do not want to block we simply ignore this tick in case that there is no data.
    if (error == WSAEWOULDBLOCK) {
      return;
    }

		printf("Failed to read from socket (error code: %d)\n", WSAGetLastError());
  }

  printf("[Socket] UDP message received (length: %d)\n", recvLength);

  // Get header from quic raw data.
  uint8_t type;
  uint32_t version;

  uint8_t scid[QUICHE_MAX_CONN_ID_LEN];
  size_t scid_len = sizeof(scid);

  uint8_t dcid[QUICHE_MAX_CONN_ID_LEN];
  size_t dcid_len = sizeof(dcid);

  uint8_t odcid[QUICHE_MAX_CONN_ID_LEN];
  size_t odcid_len = sizeof(odcid);

  uint8_t token[MAX_TOKEN_LEN];
  size_t token_len = sizeof(token);

  int rc = quiche_header_info((uint8_t*)pBuffer, recvLength, LOCAL_CONN_ID_LEN, &version,
                              &type, scid, &scid_len, dcid, &dcid_len,
                              token, &token_len);


  // Client has not sent a token yet -> therefore assume its a new client.
  if (token_len == 0) {
    this->negotiateVersion(version);
    this->createToken(scid, scid_len, dcid, dcid_len, &peer_addr, peer_addr_len, token, &token_len);
    printf("[QUIC] Retry packet with new token generated (token: %s)\n", hexStr((char *)token, token_len).c_str());
  }

  // Validate token.
  if (!validate_mint_token(token, token_len, &peer_addr, peer_addr_len, odcid, &odcid_len)) {
    printf("[QUIC] Client sent invalid token ??? whyyyy\n");
    return;
  }

  // Create new client if not yet happened.
  auto clientKey = hexStr((char*)dcid, dcid_len);
  auto client = clientRefs.find(clientKey);
  if (client == clientRefs.end()) {
    auto ref = quiche_accept(dcid, dcid_len, odcid, odcid_len, pConfig);

    ClientRef client;
    client.quiche_ref = ref;
    memcpy(client.dcid, dcid, dcid_len);
    memcpy(&client.addr, (void*)&peer_addr, peer_addr_len);

    clientRefs.insert(std::pair<std::string, ClientRef>(clientKey, client));

    printf("[QUIC] New client registered. (token: %s)\n", hexStr((char*)token, token_len).c_str());
  }

  // Send over all messages to quiche to handle quiche implementation.
  size_t done = quiche_conn_recv(client->second.quiche_ref, (uint8_t*)pBuffer, recvLength);

/*
  // Check if client is finally ready.
  auto isEstablished = quiche_conn_is_established(client->second.quiche_ref);
  auto isEarlyStage = quiche_conn_is_in_early_data(client->second.quiche_ref);
  if (isEstablished || isEarlyStage) {
    */
    // Printf


    // Handle all data that has to be sent over.
    /*
    quiche_stream_iter *writeable = quiche_conn_writable(client->second.quiche_ref);
    uint64_t id = 0;

    while (quiche_stream_iter_next(writeable, &id)) {
      bool finish = false;
      size_t recv_len = quiche_conn_stream_send(client->second.quiche_ref, id, pSendBuffer, sizeof(pSendBuffer), &finish);
    }
    quiche_stream_iter_free(writeable);
    */

    // Handle all data that has been received. 
    // TODO: not needed yet.
  //}
}


void QUICServer::createToken(
  const uint8_t *scid, size_t scid_len,
  const uint8_t *dcid, size_t dcid_len,
  struct sockaddr_in *addr, int addr_len,
  uint8_t *token, size_t *token_len
) {
  // Create a new token based on address and connection id.
  mint_token(dcid, dcid_len, addr, addr_len, token, token_len);

  // Create quiche retry packet with new token.
  size_t written = quiche_retry(scid, scid_len,
                                  dcid, dcid_len,
                                  dcid, dcid_len,
                                  token, *token_len,
                                  pSendBuffer, sizeof(pSendBuffer));

  // Send retry packet over udp.
  size_t sent = sendto(pServerSocket, (char*)pSendBuffer, written, 0,
                        (struct sockaddr *)addr,
                        addr_len);

  printf("[QUIC] Send retry package (size: %d, actual: %d)\n", written, sent);
}

void QUICServer::negotiateVersion(uint32_t version) {
  // TODO
}

  