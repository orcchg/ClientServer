#include <chrono>
#include <ctime>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <errno.h>
#include <unistd.h>
#include "logger.h"
#include "message.h"

struct Peer {
  int id;
  int socket;

  Peer(int id, int socket): id(id), socket(socket) {}
};

static int lastId = 0;

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
  std::vector<Peer> m_peers;

  Message getMessage(int socket, bool* is_closed);
  void sendMessage(const Message& message);
  void sendHello(int socket);

  void handleRequest(int socket);  // other thread
};

struct ServerException {};

/* Реализация всех функций-членов класса Сервера */
// --------------------------------------------------------------------------------------------------------------------
Server::Server(int port_number)
  : m_is_stopped(false) {
  std::string port = std::to_string(port_number);
  
  WSADATA wsaData;
  int result = WSAStartup(MAKEWORD(2,2), &wsaData);
  if (result != 0) {
    ERR("WSAStartup failed with error: %i", result);
    throw ServerException();
  }

  // prepare address structure
  addrinfo hints;
  addrinfo* server_info;

  memset(&hints, 0, sizeof hints);  // make sure the struct is empty
  hints.ai_family = AF_INET;        // family of IP addresses
  hints.ai_socktype = SOCK_STREAM;  // TCP stream sockets
  hints.ai_flags = AI_PASSIVE;  // use local IP address to make server fully portable

  int status = getaddrinfo(nullptr, port.c_str(), &hints, &server_info);
  if (status != 0) {
    ERR("Failed to prepare address structure[%i]: %s", status, gai_strerror(status));  // see error message
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

    m_peers.emplace_back(lastId, peer_socket);
    sendHello(peer_socket);
    ++lastId;

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

void Server::sendMessage(const Message& message) {
  size_t size = message.size() + 1;
  char* raw = new char[size];
  memset(raw, 0, size);
  message.raw(raw);

  for (auto& it : m_peers) {
    if (it.id != message.id) {
      send(it.socket, raw, size, 0);
    }
  }

  delete [] raw;  raw = nullptr;
}

void Server::sendHello(int socket) {
  std::string id_str = std::to_string(lastId);
  send(socket, id_str.c_str(), id_str.length(), 0);
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

    if (message == Message::EMPTY) {
      // ignore empty message
      continue;
    }

    std::cout << message << std::endl;
    sendMessage(message);
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

