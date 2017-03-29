#ifndef MESSAGE__H__
#define MESSAGE__H__

#define MESSAGE_SIZE 4096

#include <ostream>
#include <string>
#include <cstring>

struct ParseException {};

struct Message {
  int id;
  std::string login;
  std::string text;

  static Message EMPTY;

  static Message parse(char* raw);

  void raw(char* buffer) const;
  size_t size() const;

  bool operator == (const Message& rhs) const;
};

std::ostream& operator << (std::ostream& out, const Message msg) {
  out << "Message{id=" << msg.id << ", login=" << msg.login << ", text=" << msg.text << "}";
  return out;
}

Message Message::EMPTY;

Message Message::parse(char* raw) {
  char* id_str = strstr(raw, "@@");
  int id_len = id_str - raw;
  char id_buf[8];
  memset(id_buf, 0, 8);
  strncpy(id_buf, raw, id_len);
  int id = std::atoi(id_buf);

  char* text = strstr(raw, "##");
  char login[64];
  memset(login, 0, 64);
  strncpy(login, raw + id_len + 2, text - raw - id_len - 2);

  Message message;
  message.id = id;
  message.login = std::string(login);
  message.text = std::string(text + 2);
  return message;
}

void Message::raw(char* buffer) const {
  int offset = 0;
  std::string id_str = std::to_string(id);
  strncpy(buffer, id_str.c_str(), id_str.length());
  offset += id_str.length();
  strncpy(buffer + offset, "@@", 2);
  offset += 2;
  strncpy(buffer + offset, login.c_str(), login.length());
  offset += login.length();
  strncpy(buffer + offset, "##", 2);
  offset += 2;
  strncpy(buffer + offset, text.c_str(), text.length());
  offset += text.length();
  strncpy(buffer + offset, "\0", 1);
  MSG("Raw message: %s", buffer);
}

size_t Message::size() const {
  return std::to_string(id).length() + login.length() + 4 + text.length();
}

bool Message::operator == (const Message& rhs) const {
  return login == rhs.login && text == rhs.text;
}

#endif  // MESSAGE__H__

