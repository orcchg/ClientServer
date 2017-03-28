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

/* Объявление класса Клиента */
// --------------------------------------------------------------------------------------------------------------------
class Client {
public:
  Client(const std::string& name, const std::string& config_file);
  ~Client();

  void init();
  void run();

private:
  int m_socket;
  std::string m_ip_address;
  std::string m_port;

  bool m_is_connected;
  bool m_is_stopped;

  std::string m_name;

  bool readConfiguration(const std::string& config_file);
  Message getMessage(int socket, bool* is_closed);
  void receiverThread();
  void end();
};

struct ClientException {};

/* Реализация всех функций-членов класса Клиента */
// --------------------------------------------------------------------------------------------------------------------
Client::Client(const std::string& name, const std::string& config_file)
  : m_socket(-1), m_ip_address(""), m_port("http"), m_is_connected(false), m_is_stopped(false), m_name(name) {
  if (!readConfiguration(config_file)) {
    throw ClientException();
  }
}

Client::~Client() {
}

// ----------------------------------------------
void Client::init() {
  // prepare address structure
  addrinfo hints;
  addrinfo* server_info;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  int status = getaddrinfo(m_ip_address.c_str(), m_port.c_str(), &hints, &server_info);
  if (status != 0) {
    ERR("Failed to prepare address structure: %s", gai_strerror(status));  // see error message
    throw ClientException();
  }

  // establish connection
  addrinfo* ptr = server_info;

  for (; ptr != nullptr; ptr = ptr->ai_next) {  // loop through all the results
    if ((m_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      continue;  // failed to get connection socket
    }
    if (connect(m_socket, server_info->ai_addr, server_info->ai_addrlen) == -1) {
      close(m_socket);
      continue;  // failed to connect to a particular server
    }
    break;  // connect to the first particular server we can
  }

  if (ptr == nullptr) {
    ERR("Failed to connect to Server");
    m_is_connected = false;
  } else {
    m_is_connected = true;
  }

  freeaddrinfo(server_info);  // release address stucture and remove from linked list
}

// ----------------------------------------------
void Client::run() {
  if (!m_is_connected) {
    ERR("No connection established to Server");
    throw ClientException();
  }

  std::thread t(&Client::receiverThread, this);
  t.detach();

  Message message;
  message.login = m_name;

  std::ostringstream oss;
  std::cin.ignore();
  while (!m_is_stopped && getline(std::cin, message.text)) {
    //sendMessage(message);
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

    printf("\e[5;00;33m%s\e[m :: \e[5;01;37m%s\e[m: %s\n", timestamp.c_str(), message.login.c_str(), message.text.c_str());
  }  // while loop ending

  end();
}

// ----------------------------------------------
void Client::end() {
  DBG("Client closing...");
  m_is_stopped = true;  // stop background receiver thread if any
  close(m_socket);
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


