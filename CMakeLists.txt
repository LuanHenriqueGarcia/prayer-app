cmake_minimum_required(VERSION 3.21)

# Version of the native app. The release tag must be v<this version>.
project(PrayerApp VERSION 2.0.0 LANGUAGES CXX RC)

if(NOT MSVC)
  message(FATAL_ERROR "PrayerApp native builds with MSVC only (WebView2 static loader + C++/WinRT).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# compile_commands.json feeds VS Code IntelliSense (see .vscode/settings.json).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static CRT: the .exe runs on a clean Windows with no VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- WebView2 SDK (headers + static loader), fetched once from NuGet and hash-pinned ---
set(WV2_VERSION "1.0.4022.49")
set(WV2_SHA256 "ee9de67e5bb9ef3a96c5689b2efc8188e2df160a0e79234c0404243782fde5fb")
set(WV2_DIR "${CMAKE_BINARY_DIR}/webview2-${WV2_VERSION}")
if(NOT EXISTS "${WV2_DIR}/build/native/include/WebView2.h")
  set(WV2_PKG "${CMAKE_BINARY_DIR}/webview2-${WV2_VERSION}.nupkg")
  set(WV2_URL "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${WV2_VERSION}/microsoft.web.webview2.${WV2_VERSION}.nupkg")
  file(DOWNLOAD "${WV2_URL}" "${WV2_PKG}" TLS_VERIFY ON STATUS WV2_STATUS)
  list(GET WV2_STATUS 0 WV2_CODE)
  if(NOT WV2_CODE EQUAL 0)
    # Networks with TLS inspection: CMake's curl does not see the Windows certificate store; PowerShell does.
    message(STATUS "CMake download failed (${WV2_STATUS}); retrying through PowerShell")
    execute_process(COMMAND powershell -NoProfile -Command
      "[Net.ServicePointManager]::SecurityProtocol='Tls12'; Invoke-WebRequest -UseBasicParsing -Uri '${WV2_URL}' -OutFile '${WV2_PKG}'"
      RESULT_VARIABLE WV2_PS)
    if(NOT WV2_PS EQUAL 0)
      message(FATAL_ERROR "Could not download the WebView2 SDK")
    endif()
  endif()
  file(SHA256 "${WV2_PKG}" WV2_GOT)
  if(NOT WV2_GOT STREQUAL WV2_SHA256)
    file(REMOVE "${WV2_PKG}")
    message(FATAL_ERROR "WebView2 SDK hash mismatch: ${WV2_GOT}")
  endif()
  file(ARCHIVE_EXTRACT INPUT "${WV2_PKG}" DESTINATION "${WV2_DIR}")
endif()

configure_file(src/version.h.in "${CMAKE_BINARY_DIR}/gen/version.h" @ONLY)

add_executable(PrayerApp WIN32
  src/main.cpp
  src/util.cpp
  src/json.cpp
  src/logger.cpp
  src/printers.cpp
  src/pdfprint.cpp
  src/htmlprint.cpp
  src/wsserver.cpp
  src/actions.cpp
  src/updater.cpp
  src/logviewer.cpp
  src/tester.cpp
  res/app.rc
  res/app.manifest)

target_include_directories(PrayerApp PRIVATE
  src res
  "${CMAKE_BINARY_DIR}/gen"
  "${WV2_DIR}/build/native/include")

target_compile_definitions(PrayerApp PRIVATE
  UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
  _WIN32_WINNT=0x0A00)

target_compile_options(PrayerApp PRIVATE /utf-8 /W3 /EHsc /permissive- /Zi)
# PDB kept beside the build (not shipped) so crash offsets in the log can be resolved.
target_link_options(PrayerApp PRIVATE /DEBUG /OPT:REF /OPT:ICF /MAP)

target_link_libraries(PrayerApp PRIVATE
  "${WV2_DIR}/build/native/x64/WebView2LoaderStatic.lib"
  ws2_32 winhttp winspool comctl32 shell32 shlwapi bcrypt crypt32 iphlpapi
  ole32 oleaut32 version advapi32 gdi32 user32 windowsapp)
