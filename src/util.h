#pragma once
#include <windows.h>
#include <string>

std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& wide);
std::wstring lowerW(std::wstring s);

std::string base64Encode(const void* data, size_t len);
bool base64Decode(const std::string& text, std::string& out);
std::string sha1Raw(const std::string& data);           // 20 raw bytes
bool sha256HexOfFile(const std::wstring& path, std::string& hex);

std::wstring winErrorText(DWORD code);
std::wstring exePath();
std::wstring appTempDir();                                // %TEMP%\prayer (created)
std::wstring localDateTime(const wchar_t* dateFmt = L"dd/MM/yyyy", const wchar_t* timeFmt = L"HH:mm:ss");
