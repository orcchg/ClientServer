#include <chrono>
#include <ctime>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x501
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#include <errno.h>
#include <unistd.h>
#include "logger.h"
#include "message.h"

/* Объявление класса Клиента */
// --------------------------------------------------------------------------------------------------------------------
class Client {
public:
  Client(const std::string& name, const std::string& config_file);
  ~Client();

  void init();
  void run();

private:
  int m_id;
  SOCKET m_socket;
  std::string m_ip_address;
  std::string m_port;
  bool m_is_connected;
  bool m_is_stopped;

  std::string m_name;

  bool readConfiguration(const std::string& config_file);
  void receiverThread();
  void end();

  Message getMessage(int socket, bool* is_closed);
  void sendMessage(const Message& message) const;
};

struct ClientException {};

/* Реализация всех функций-членов класса Клиента */
// --------------------------------------------------------------------------------------------------------------------
Client::Client(const std::string& name, const std::string& config_file)
  : m_id(-1), m_socket(-1), m_ip_address(""), m_port("http"), m_is_connected(false), m_is_stopped(false), m_name(name) {
  if (!readConfiguration(config_file)) {
    throw ClientException();
  }
}

Client::~Client() {
}

// ----------------------------------------------
void Client::init() {
	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (result != 0) {
		ERR("WSAStartup failed with error: %i", result);
		throw ClientException();
	}
	
	
  // prepare address structure
  addrinfo hints;
  addrinfo* server_info;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  int status = getaddrinfo(m_ip_address.c_str(), m_port.c_str(), &hints, &server_info);
  if (status != 0) {
    ERR("Failed to prepare address structure: %s", gai_strerror(status));  // see error message
    WSACleanup();
	throw ClientException();
  }

  // establish connection
  addrinfo* ptr = server_info;

  for (; ptr != nullptr; ptr = ptr->ai_next) {  // loop through all the results
    if ((m_socket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) == INVALID_SOCKET) {
      continue;  // failed to get connection socket
    }
    if (connect(m_socket, ptr->ai_addr, ptr->ai_addrlen) == SOCKET_ERROR) {
      close(m_socket);
	  m_socket = INVALID_SOCKET;
      continue;  // failed to connect to a particular server
    }
    break;  // connect to the first particular server we can
  }

  freeaddrinfo(server_info);  // release address stucture and remove from linked list
  
  if (m_socket == INVALID_SOCKET) {
    ERR("Failed to connect to Server");
    WSACleanup();
	throw ClientException();
  }
  
  m_is_connected = true;
}

// ----------------------------------------------
void Client::run() {
  if (!m_is_connected) {
    ERR("No connection established to Server");
    throw ClientException();
  }

  // receive id of this peer from Server
  char buffer[64];
  memset(buffer, 0, 64);
  int read_bytes = recv(m_socket, buffer, MESSAGE_SIZE, 0);
  if (read_bytes <= 0) {
    DBG("Connection closed: failed to receive hello from Server");
    end();
    return;
  }
  m_id = std::atoi(buffer);
  printf("Server has assigned id to this peer: %i\n", m_id);

  std::thread t(&Client::receiverThread, this);
  t.detach();

  Message message;
  message.id = m_id;
  message.login = m_name;

  std::ostringstream oss;
  while (!m_is_stopped && getline(std::cin, message.text)) {
    if (message.text == "!exit") {
      m_is_stopped = true;
      return;
    }
    sendMessage(message);
  }
}

// ----------------------------------------------
bool Client::readConfiguration(const std::string& config_file) {
  bool result = true;
  std::fstream fs;
  fs.open(config_file, std::fstream::in);

  if (fs.is_open()) {
    std::string line;
    // ip address
    std::getline(fs, line);
    int i1 = line.find_first_of(' ');
    m_ip_address = line.substr(i1 + 1);
    DBG("IP address: %s", m_ip_address.c_str());
    // port
    std::getline(fs, line);
    int i2 = line.find_first_of(' ');
    m_port = line.substr(i2 + 1);
    DBG("Port: %s", m_port.c_str());
    fs.close();
  } else {
    ERR("Failed to open configure file: %s", config_file.c_str());
    result = false;
  }
  return result;
}

// ----------------------------------------------
void Client::receiverThread() {
  while (!m_is_stopped) {
    // peers' messages
    Message message = getMessage(m_socket, &m_is_stopped);

    std::chrono::time_point<std::chrono::system_clock> end = std::chrono::system_clock::now();
    std::time_t end_time = std::chrono::system_clock::to_time_t(end);
    std::string timestamp(std::ctime(&end_time));
    int i1 = timestamp.find_last_of('\n');
    timestamp = timestamp.substr(0, i1);

    printf("%s :: %s: %s\n", timestamp.c_str(), message.login.c_str(), message.text.c_str());
  }  // while loop ending

  end();
}

// ----------------------------------------------
void Client::end() {
  DBG("Client closing...");
  m_is_stopped = true;  // stop background receiver thread if any
  close(m_socket);
  WSACleanup();
}

// ----------------------------------------------
Message Client::getMessage(int socket, bool* is_closed) {
  char buffer[MESSAGE_SIZE];
  memset(buffer, 0, MESSAGE_SIZE);
  int read_bytes = recv(socket, buffer, MESSAGE_SIZE, 0);
  if (read_bytes <= 0) {
    if (read_bytes == -1) {
      ERR("get response error: %s", strerror(errno));
    } else if (read_bytes == 0) {
      printf("\e[5;00;31mSystem: Server shutdown\e[m\n");
    }
    DBG("Connection closed");
    *is_closed = true;
    return Message::EMPTY;
  }
  try {
    DBG("Raw response[%i bytes]: %.*s", read_bytes, (int) read_bytes, buffer);
    return Message::parse(buffer);
  } catch (ParseException exception) {
    FAT("ParseException on raw response[%i bytes]: %.*s", read_bytes, (int) read_bytes, buffer);
    return Message::EMPTY;
  }
}

void Client::sendMessage(const Message& message) const {
  size_t size = message.size() + 1;
  char* raw = new char[size];
  memset(raw, 0, size);
  message.raw(raw);
  send(m_socket, raw, size, 0);
  delete [] raw;  raw = nullptr;
}

/* Точка входа в программу */
// --------------------------------------------------------------------------------------------------------------------
int main(int argc, char** argv) {
  // read name
  std::string name = "user";
  if (argc >= 2) {
    char buffer[64];
    memset(buffer, 0, 64);
    strncpy(buffer, argv[1], strlen(argv[1]));
    name = std::string(buffer);
  }

  // read configuration
  std::string config_file = "local.cfg";
  if (argc >= 3) {
    char buffer[64];
    memset(buffer, 0, 64);
    strncpy(buffer, argv[1], strlen(argv[2]));
    config_file = std::string(buffer);
  }
  DBG("Configuration from file: %s", config_file.c_str());

  // start client
  Client client(name, config_file);
  client.init();
  client.run();
  return 0;
}


