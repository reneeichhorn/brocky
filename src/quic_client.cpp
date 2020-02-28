#include "quic_client.h"

void QUICClient::cleanup() {
  if (pQuicheRef) {
    quiche_conn_free(pQuicheRef);
    pQuicheRef = nullptr;
  }

  if (pConfig) {
    quiche_config_free(pConfig);
    pConfig = nullptr;
  }

  if (pPeer) {
    freeaddrinfo(pPeer);
    pPeer = nullptr;
  }
}

static void debug_log(const char *line, void *argp) {
  //fprintf(stderr, "[QUICHE DEBUG] %s\n", line);
}

bool QUICClient::initialize() {
  // Initialize quiche config
  quiche_enable_debug_logging(debug_log, NULL);
  pConfig = quiche_config_new(QUICHE_PROTOCOL_VERSION);
  if (!pConfig) {
    printf("Failed to create quiche configuration\n");
    return false;
  }

  // Apply configuration.
  quiche_config_load_cert_chain_from_pem_file(pConfig, "certs/cert.crt");
  quiche_config_load_priv_key_from_pem_file(pConfig, "certs/cert.key");
  quiche_config_set_application_protos(pConfig,
    (uint8_t *) "\x05hq-27\x05hq-25\x05hq-24\x05hq-23\x08http/0.9", 21);
  quiche_config_set_max_packet_size(pConfig, MAX_DATAGRAM_SIZE);
  quiche_config_enable_early_data(pConfig);
  quiche_config_verify_peer(pConfig, false);
  quiche_config_set_initial_max_data(pConfig, 0x0FFFFFFFFFFFFFFF);
  quiche_config_set_initial_max_stream_data_bidi_local(pConfig, 0x0FFFFFFFFFFFFFFF);
  quiche_config_set_initial_max_stream_data_bidi_remote(pConfig, 0x0FFFFFFFFFFFFFFF);
  quiche_config_set_initial_max_streams_bidi(pConfig, 0x0FFFFFFFFFFFFFFF);
  //quiche_config_set_max_idle_timeout(pConfig, 5000);
  /*
  quiche_config_set_max_packet_size(pConfig, MAX_DATAGRAM_SIZE);
  quiche_config_set_initial_max_data(pConfig, 10000000);
  quiche_config_set_initial_max_stream_data_bidi_local(pConfig, 1000000);
  quiche_config_set_initial_max_stream_data_bidi_remote(pConfig, 1000000);
  quiche_config_set_initial_max_streams_bidi(pConfig, 100);
  quiche_config_set_cc_algorithm(pConfig, QUICHE_CC_RENO);
  */

  // Connect to host
  struct addrinfo hints = {};
  hints.ai_family = PF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  if (getaddrinfo("192.168.178.20", "1337", &hints, &pPeer) != 0) {
    perror("[UDP] Failed to resolve host.");
    return false;
  }

  socketRef = socket(pPeer->ai_family, SOCK_DGRAM, 0);
  if (socketRef < 0) {
    perror("[UDP] Failed to create udp socket\n");
    return false;
  }

  if (fcntl(socketRef, F_SETFL, O_NONBLOCK) != 0) {
    perror("[UDP] Failed to make socket non-blocking\n");
    return -1;
  }

  if (connect(socketRef, pPeer->ai_addr, pPeer->ai_addrlen) < 0) {
    perror("[UDP] Failed to connect to server\n");
    return false;
  }

  // Create random scid 
  uint8_t scid[LOCAL_CONN_ID_LEN];
  int rng = open("/dev/urandom", O_RDONLY);
  if (rng < 0) {
      perror("[QUIC] Failed to open /dev/urandom\n");
      return -1;
  }

  ssize_t rand_len = read(rng, &scid, sizeof(scid));
  if (rand_len < 0) {
      perror("[QUIC] Failed to create connection ID\n");
      return -1;
  }

  // Connect with quiche
  pQuicheRef = quiche_connect("192.168.178.20", (const uint8_t *) scid,
                                      sizeof(scid), pConfig);
  if (pQuicheRef == NULL) {
    fprintf(stderr, "[QUIC] Failed to create connection\n");
    return -1;
  }
  
  return true;
}

void QUICClient::tick() {
  auto isEstablished = quiche_conn_is_established(pQuicheRef);
  auto isEarlyStage = quiche_conn_is_in_early_data(pQuicheRef);
  auto isClosed = quiche_conn_is_closed(pQuicheRef);
  //printf("Client state %s %s %s\n", isEstablished ? "true" : "false", isEarlyStage ? "true" : "false", isClosed ? "true" : "false");

  // Send out all QUIC packets over UDP.
  while (true) {
    ssize_t written = quiche_conn_send(pQuicheRef, pSendBuffer, sizeof(pSendBuffer));
    if (written != QUICHE_ERR_DONE) {
      if (written < 0) {
        fprintf(stderr, "[QUIC] Failed to create packet: %zd\n", written);
        return;
      }

      ssize_t sent = send(socketRef, pSendBuffer, written, 0);
      if (sent != written) {
        perror("[UDP] Failed to send packet.\n");
        return;
      }

      //printf("[QUIC] Send QUIC packet to server (size: %zd)\n", written);
    } else {
      break;
    }
  }

  // Handle all incoming packets to QUIC
  while (true) {
    ssize_t read = recv(socketRef, pSendBuffer, sizeof(pSendBuffer), 0);

    if (read < 0) {
      if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
        break;
      }

      perror("[UDP] Failed to read: ");
      return;
    }

    //fprintf(stderr, "[UDP] Received packet (size: %zd)\n", read);

    ssize_t done = quiche_conn_recv(pQuicheRef, pSendBuffer, read);
    //printf("[QUIC] Handled incoming packet (size: %zd)\n", done);

    if (done == QUICHE_ERR_DONE) {
      break;
    }

    if (done < 0) {
      fprintf(stderr, "[QUIC] Failed to process packet\n");
      return;
    }
  }

  static bool hasSent = false;
  if (quiche_conn_is_established(pQuicheRef) && !hasSent) {
    const static uint8_t r[] = "GET /raw/stream.h264\r\n";
    auto sent = quiche_conn_stream_send(pQuicheRef, 4, r, sizeof(r), true);
    if (sent < 0) {
        fprintf(stderr, "Failed to send HTTP request (error: %d)\n", sent);
        return;
    }
    printf("[QUIC] Send request to retrieve raw stream\n");
    hasSent = true;
  }

  // Handle packets after QUIC parsed
  static int received = 0;
  static int count = 0;

  if (quiche_conn_is_established(pQuicheRef)) {
    uint64_t s = 0;

    quiche_stream_iter *readable = quiche_conn_readable(pQuicheRef);

    while (quiche_stream_iter_next(readable, &s)) {
      bool fin = false;
      ssize_t recv_len = quiche_conn_stream_recv(pQuicheRef, s,
                                                  (uint8_t*)pBuffer, sizeof(pBuffer),
                                                  &fin);
      if (recv_len < 0) {
        break;
      }

      count++;
      received += recv_len;

      if (fin) {
        printf("[QUIC] FIN: Received %d packets that contained a total of %d\n", count, received);
        received = 0;
        count = 0;
      } else {
        printf("[QUIC] Received %d packets that contained a total of %d\n", 1, recv_len);
      };
    }

    quiche_stream_iter_free(readable);
  }
}