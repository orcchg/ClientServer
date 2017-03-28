#ifndef MESSAGE__H__
#define MESSAGE__H__

#define MESSAGE_SIZE 4096

#include <string>
#include <cstring>

struct ParseException {};

struct Message {
  std::string login;
  std::string text;

  static Message EMPTY;

  static Message parse(char* raw);

  void raw(char* buffer);
};

Message Message::EMPTY;

Message Message::parse(char* raw) {
  char* text = strstr(raw, "##");
  char login[64];
  memset(login, 0, 64);
  strncpy(login, raw, text - raw);

  Message message;
  message.login = std::string(login);
  message.text = std::string(text);
  return message;
}

void Message::raw(char* buffer) {
  strncpy(buffer, login.c_str(), login.length());
  strncpy(buffer + login.length(), text.c_str(), text.length());
}

#endif  // MESSAGE__H__

