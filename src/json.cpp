#include "json.h"
#include "util.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

Json::Json(const std::wstring& v) : type(Str), s(narrow(v)) {}

Json& Json::set(const std::string& key, Json value) {
  type = Obj;
  for (auto& kv : o) {
    if (kv.first == key) { kv.second = std::move(value); return *this; }
  }
  o.emplace_back(key, std::move(value));
  return *this;
}

Json& Json::push(Json value) {
  type = Arr;
  a.push_back(std::move(value));
  return *this;
}

const Json* Json::find(const char* key) const {
  if (type != Obj) return nullptr;
  for (auto& kv : o) {
    if (kv.first == key) return &kv.second;
  }
  return nullptr;
}

std::string Json::str(const char* key) const {
  const Json* j = find(key);
  return j && j->type == Str ? j->s : std::string();
}

bool Json::truthy() const {
  switch (type) {
    case Null: return false;
    case Bool: return b;
    case Num: return n != 0 && !std::isnan(n);
    case Str: return !s.empty();
    default: return true;
  }
}

static void dumpString(const std::string& s, std::string& out) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += (char)c;
        }
    }
  }
  out += '"';
}

static void dumpValue(const Json& j, std::string& out) {
  switch (j.type) {
    case Json::Null: out += "null"; break;
    case Json::Bool: out += j.b ? "true" : "false"; break;
    case Json::Num: {
      char buf[40];
      if (std::isfinite(j.n) && j.n == std::floor(j.n) && std::fabs(j.n) < 9e15) snprintf(buf, sizeof buf, "%lld", (long long)j.n);
      else if (std::isfinite(j.n)) snprintf(buf, sizeof buf, "%.17g", j.n);
      else snprintf(buf, sizeof buf, "null");
      out += buf;
      break;
    }
    case Json::Str: dumpString(j.s, out); break;
    case Json::Arr: {
      out += '[';
      for (size_t i = 0; i < j.a.size(); i++) {
        if (i) out += ',';
        dumpValue(j.a[i], out);
      }
      out += ']';
      break;
    }
    case Json::Obj: {
      out += '{';
      for (size_t i = 0; i < j.o.size(); i++) {
        if (i) out += ',';
        dumpString(j.o[i].first, out);
        out += ':';
        dumpValue(j.o[i].second, out);
      }
      out += '}';
      break;
    }
  }
}

std::string Json::dump() const {
  std::string out;
  dumpValue(*this, out);
  return out;
}

namespace {

struct Parser {
  const char* p;
  const char* end;
  int depth = 0;

  void ws() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
  }

  static void appendUtf8(std::string& s, unsigned cp) {
    if (cp < 0x80) {
      s += (char)cp;
    } else if (cp < 0x800) {
      s += (char)(0xC0 | (cp >> 6));
      s += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      s += (char)(0xE0 | (cp >> 12));
      s += (char)(0x80 | ((cp >> 6) & 0x3F));
      s += (char)(0x80 | (cp & 0x3F));
    } else {
      s += (char)(0xF0 | (cp >> 18));
      s += (char)(0x80 | ((cp >> 12) & 0x3F));
      s += (char)(0x80 | ((cp >> 6) & 0x3F));
      s += (char)(0x80 | (cp & 0x3F));
    }
  }

  bool hex4(unsigned& v) {
    if (end - p < 4) return false;
    v = 0;
    for (int i = 0; i < 4; i++) {
      char c = *p++;
      v <<= 4;
      if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
      else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
      else return false;
    }
    return true;
  }

  bool string(std::string& out) {
    if (p >= end || *p != '"') return false;
    p++;
    while (p < end) {
      char c = *p++;
      if (c == '"') return true;
      if (c != '\\') { out += c; continue; }
      if (p >= end) return false;
      char e = *p++;
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            unsigned lo = 0;
            const char* save = p;
            if (end - p >= 6 && p[0] == '\\' && p[1] == 'u' && (p += 2, hex4(lo)) && lo >= 0xDC00 && lo <= 0xDFFF) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else {
              p = save;
              cp = 0xFFFD;
            }
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;
          }
          appendUtf8(out, cp);
          break;
        }
        default: return false;
      }
    }
    return false;
  }

  bool literal(const char* word) {
    size_t n = strlen(word);
    if ((size_t)(end - p) < n || memcmp(p, word, n) != 0) return false;
    p += n;
    return true;
  }

  bool value(Json& out) {
    ws();
    if (p >= end || ++depth > 200) return false;
    bool ok = false;
    char c = *p;
    if (c == '{') {
      p++;
      out = Json::object();
      ws();
      if (p < end && *p == '}') { p++; ok = true; }
      else {
        for (;;) {
          ws();
          std::string key;
          if (!string(key)) break;
          ws();
          if (p >= end || *p != ':') break;
          p++;
          Json v;
          if (!value(v)) break;
          out.o.emplace_back(std::move(key), std::move(v));
          ws();
          if (p < end && *p == ',') { p++; continue; }
          if (p < end && *p == '}') { p++; ok = true; }
          break;
        }
      }
    } else if (c == '[') {
      p++;
      out = Json::array();
      ws();
      if (p < end && *p == ']') { p++; ok = true; }
      else {
        for (;;) {
          Json v;
          if (!value(v)) break;
          out.a.push_back(std::move(v));
          ws();
          if (p < end && *p == ',') { p++; continue; }
          if (p < end && *p == ']') { p++; ok = true; }
          break;
        }
      }
    } else if (c == '"') {
      out.type = Json::Str;
      ok = string(out.s);
    } else if (c == 't') {
      ok = literal("true");
      out = Json(true);
    } else if (c == 'f') {
      ok = literal("false");
      out = Json(false);
    } else if (c == 'n') {
      ok = literal("null");
      out = Json();
    } else if (c == '-' || (c >= '0' && c <= '9')) {
      const char* start = p;
      while (p < end && (*p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E' || (*p >= '0' && *p <= '9'))) p++;
      std::string num(start, p);
      char* stop = nullptr;
      double d = strtod(num.c_str(), &stop);
      ok = stop && *stop == '\0';
      out = Json(d);
    }
    depth--;
    return ok;
  }
};

}  // namespace

bool Json::parse(const std::string& text, Json& out) {
  Parser ps{text.data(), text.data() + text.size()};
  if (!ps.value(out)) return false;
  ps.ws();
  return ps.p == ps.end;
}
