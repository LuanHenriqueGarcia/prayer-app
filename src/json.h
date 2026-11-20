#pragma once
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Small JSON value: enough for the WebSocket protocol, the log metadata and the GitHub API.
struct Json {
  enum Type { Null, Bool, Num, Str, Arr, Obj };
  Type type = Null;
  bool b = false;
  double n = 0;
  std::string s;                                   // UTF-8, may contain NUL bytes (ESC/POS payloads)
  std::vector<Json> a;
  std::vector<std::pair<std::string, Json>> o;     // insertion order kept, like JSON.stringify

  Json() {}
  Json(bool v) : type(Bool), b(v) {}
  Json(const char* v) : type(Str), s(v ? v : "") {}
  Json(const std::string& v) : type(Str), s(v) {}
  Json(const std::wstring& v);
  template <class T, std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>, int> = 0>
  Json(T v) : type(Num), n((double)v) {}

  static Json object() { Json j; j.type = Obj; return j; }
  static Json array() { Json j; j.type = Arr; return j; }

  Json& set(const std::string& key, Json value);
  Json& push(Json value);

  bool isNull() const { return type == Null; }
  bool isStr() const { return type == Str; }
  bool isObj() const { return type == Obj; }
  const Json* find(const char* key) const;
  std::string str(const char* key) const;          // "" when missing or not a string
  bool truthy() const;                             // JavaScript-like truthiness

  std::string dump() const;
  static bool parse(const std::string& text, Json& out);
};
