#include "util.h"
#include <bcrypt.h>
#include <wincrypt.h>
#include <vector>

std::wstring widen(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring lowerW(std::wstring s) {
  if (!s.empty()) CharLowerBuffW(s.data(), (DWORD)s.size());
  return s;
}

std::string base64Encode(const void* data, size_t len) {
  DWORD n = 0;
  const DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
  if (!CryptBinaryToStringA((const BYTE*)data, (DWORD)len, flags, nullptr, &n)) return "";
  std::string out(n, '\0');
  if (!CryptBinaryToStringA((const BYTE*)data, (DWORD)len, flags, out.data(), &n)) return "";
  out.resize(n);
  return out;
}

bool base64Decode(const std::string& text, std::string& out) {
  // Accepts a data URI ("data:application/pdf;base64,....") as well as the bare payload.
  size_t start = 0;
  if (text.compare(0, 5, "data:") == 0) {
    size_t comma = text.find(',');
    if (comma != std::string::npos) start = comma + 1;
  }
  DWORD n = 0;
  const char* p = text.data() + start;
  DWORD len = (DWORD)(text.size() - start);
  if (!CryptStringToBinaryA(p, len, CRYPT_STRING_BASE64, nullptr, &n, nullptr, nullptr)) return false;
  out.assign(n, '\0');
  if (!CryptStringToBinaryA(p, len, CRYPT_STRING_BASE64, (BYTE*)out.data(), &n, nullptr, nullptr)) return false;
  out.resize(n);
  return true;
}

std::string sha1Raw(const std::string& data) {
  std::string out(20, '\0');
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0) return "";
  NTSTATUS st = BCryptHash(alg, nullptr, 0, (PUCHAR)data.data(), (ULONG)data.size(), (PUCHAR)out.data(), 20);
  BCryptCloseAlgorithmProvider(alg, 0);
  return st < 0 ? "" : out;
}

bool sha256HexOfFile(const std::wstring& path, std::string& hex) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE h = nullptr;
  bool ok = false;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
      BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) >= 0) {
    std::vector<BYTE> buf(1 << 16);
    DWORD got = 0;
    ok = true;
    while (ReadFile(f, buf.data(), (DWORD)buf.size(), &got, nullptr) && got > 0) {
      if (BCryptHashData(h, buf.data(), got, 0) < 0) { ok = false; break; }
    }
    BYTE digest[32];
    if (ok && BCryptFinishHash(h, digest, 32, 0) >= 0) {
      static const char* d = "0123456789abcdef";
      hex.clear();
      for (BYTE b : digest) { hex += d[b >> 4]; hex += d[b & 15]; }
    } else {
      ok = false;
    }
  }
  if (h) BCryptDestroyHash(h);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  CloseHandle(f);
  return ok;
}

std::wstring winErrorText(DWORD code) {
  wchar_t* buf = nullptr;
  DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, code, 0, (LPWSTR)&buf, 0, nullptr);
  std::wstring s = n ? std::wstring(buf, n) : L"erro";
  if (buf) LocalFree(buf);
  while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ' || s.back() == L'.')) s.pop_back();
  return s + L" (" + std::to_wstring(code) + L")";
}

std::wstring exePath() {
  std::wstring p(MAX_PATH, L'\0');
  for (;;) {
    DWORD n = GetModuleFileNameW(nullptr, p.data(), (DWORD)p.size());
    if (n < p.size()) { p.resize(n); return p; }
    p.resize(p.size() * 2);
  }
}

std::wstring appTempDir() {
  wchar_t tmp[MAX_PATH + 2];
  DWORD n = GetTempPathW(MAX_PATH + 1, tmp);
  std::wstring base(tmp, n);                       // ends with a backslash
  std::wstring dir = base + L"prayer";
  if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
    if (!base.empty()) base.pop_back();
    return base;                                   // last resort: plain %TEMP%
  }
  return dir;
}

std::wstring localDateTime(const wchar_t* dateFmt, const wchar_t* timeFmt) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t d[64] = L"", h[64] = L"";
  GetDateFormatW(LOCALE_INVARIANT, 0, &t, dateFmt, d, 64);
  GetTimeFormatW(LOCALE_INVARIANT, 0, &t, timeFmt, h, 64);
  return std::wstring(d) + L" " + h;
}
