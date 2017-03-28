#include <chrono>
#include <ctime>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "logger.h"
#include "message.h"

/* Объявление класса Сервера */
// --------------------------------------------------------------------------------------------------------------------
class Server {
public:
  Server(int port_number);
  ~Server();

  void run();
  void stop();

private:
  bool m_is_stopped;
  int m_socket;

  Message getMessage(int socket, bool* is_closed);
  void handleRequest(int socket);  // other thread
};

struct ServerException {};

/* Реализация всех функций-членов класса Сервера */
// --------------------------------------------------------------------------------------------------------------------
Server::Server(int port_number)
  : m_is_stopped(false) {
  std::string port = std::to_string(port_number);

  // prepare address structure
  addrinfo hints;
  addrinfo* server_info;

  memset(&hints, 0, sizeof hints);  // make sure the struct is empty
  hints.ai_family = AF_INET;        // family of IP addresses
  hints.ai_socktype = SOCK_STREAM;  // TCP stream sockets
  hints.ai_flags = AI_PASSIVE;  // use local IP address to make server fully portable

  int status = getaddrinfo(nullptr, port.c_str(), &hints, &server_info);
  if (status != 0) {
    ERR("Failed to prepare address structure: %s", gai_strerror(status));  // see error message
    throw ServerException();
  }

  // get a socket
  m_socket = socket(server_info->ai_family, server_info->ai_socktype, server_info->ai_protocol);

  if (m_socket < 0) {
    ERR("Failed to open socket");
    throw ServerException();
  }

  // bind socket with address structure
  if (bind(m_socket, server_info->ai_addr, server_info->ai_addrlen) < 0) {
    ERR("Failed to bind socket to the address");
    throw ServerException();
  }

  freeaddrinfo(server_info);  // release address stucture and remove from linked list

  // when the socket of a type that promises reliable delivery still has untransmitted messages when it is closed
  linger linger_opt = { 1, 0 };  // timeout 0 seconds - close socket immediately
  setsockopt(m_socket, SOL_SOCKET, SO_LINGER, &linger_opt, sizeof(linger_opt));

  // listen for incoming connections
  listen(m_socket, 20);
}

Server::~Server() {
  if (!m_is_stopped) {
    stop();
  }
}

// ----------------------------------------------
void Server::run() {
  while (!m_is_stopped) {  // server loop
    sockaddr_in peer_address_structure;
    socklen_t peer_address_structure_size = sizeof(peer_address_structure);

    // accept one pending connection, waits until a new connection comes
    int peer_socket = accept(m_socket, reinterpret_cast<sockaddr*>(&peer_address_structure), &peer_address_structure_size);
    if (peer_socket < 0) {
      ERR("Failed to open new socket for data transfer");
      continue;  // skip failed connection
    }

    // get incoming message
    std::thread t(&Server::handleRequest, this, peer_socket);
    t.detach();
  }
}

// ----------------------------------------------
void Server::stop() {
  m_is_stopped = true;
  close(m_socket);
}

// ----------------------------------------------
Message Server::getMessage(int socket, bool* is_closed) {
  char buffer[MESSAGE_SIZE];
  memset(buffer, 0, MESSAGE_SIZE);
  int read_bytes = recv(socket, buffer, MESSAGE_SIZE, 0);
  if (read_bytes <= 0) {
    if (read_bytes == -1) {
      ERR("get request error: %s", strerror(errno));
    }
    DBG("Connection closed");
    *is_closed = true;
    return Message::EMPTY;
  }
  try {
    DBG("Raw request[%i bytes]: %.*s", read_bytes, (int) read_bytes, buffer);
    return Message::parse(buffer);
  } catch (ParseException exception) {
    FAT("ParseException on raw request[%i bytes]: %.*s", read_bytes, (int) read_bytes, buffer);
    return Message::EMPTY;
  }
}

// ----------------------------------------------
void Server::handleRequest(int socket) {
  while (!m_is_stopped) {
    // peers' messages
    bool is_closed = false;
    Message message = getMessage(socket, &is_closed);
    if (is_closed) {
      DBG("Stopping peer thread...");
      close(socket);
      return;
    }

    //if (message == Message::EMPTY) {
    //  // ignore empty message
    //  continue;
    //}

    //sendMessage(message);
  }
}

/* Точка входа в программу */
// --------------------------------------------------------------------------------------------------------------------
int main(int argc, char** argv) {
  int port = 80;
  if (argc > 1) {
    port = std::atoi(argv[1]);
  }
  Server server(port);
  server.run();
  return 0;
}

