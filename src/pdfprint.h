#pragma once
#include <string>

// Prints a PDF (bytes) on a spooler queue: pages rendered by Windows.Data.Pdf at the printer
// resolution and scaled to fit the printable area (the old pdf-to-printer "fit" + monochrome).
// Must run on a thread that is not the UI thread (it blocks on WinRT async calls).
bool printPdf(const std::string& pdfBytes, const std::wstring& queueName, std::wstring& error, int& pages);
