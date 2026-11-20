#include "pdfprint.h"
#include "util.h"
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <vector>

using namespace winrt;
using namespace winrt::Windows::Data::Pdf;
using namespace winrt::Windows::Storage::Streams;

static std::vector<uint8_t> readAll(const IRandomAccessStream& s) {
  std::vector<uint8_t> data((size_t)s.Size());
  s.Seek(0);
  DataReader reader(s.GetInputStreamAt(0));
  reader.LoadAsync((uint32_t)data.size()).get();
  reader.ReadBytes(data);
  return data;
}

bool printPdf(const std::string& pdfBytes, const std::wstring& queueName, std::wstring& error, int& pages) {
  pages = 0;
  try {
    winrt::init_apartment(apartment_type::multi_threaded);
  } catch (...) {
    // already initialised on this thread
  }

  HDC dc = nullptr;
  bool docStarted = false;
  try {
    InMemoryRandomAccessStream input;
    {
      DataWriter writer(input);
      writer.WriteBytes(array_view<const uint8_t>((const uint8_t*)pdfBytes.data(), (uint32_t)pdfBytes.size()));
      writer.StoreAsync().get();
      writer.DetachStream();
    }
    input.Seek(0);
    PdfDocument doc = PdfDocument::LoadFromStreamAsync(input).get();

    dc = CreateDCW(L"WINSPOOL", queueName.c_str(), nullptr, nullptr);
    if (!dc) {
      error = L"Não foi possível abrir a impressora: " + winErrorText(GetLastError());
      return false;
    }
    const int pageW = GetDeviceCaps(dc, HORZRES), pageH = GetDeviceCaps(dc, VERTRES);
    if (pageW <= 0 || pageH <= 0) {
      error = L"A impressora não informou a área de impressão";
      DeleteDC(dc);
      return false;
    }

    DOCINFOW di{sizeof di};
    di.lpszDocName = L"PrayerApp - cupom PDF";
    if (StartDocW(dc, &di) <= 0) {
      error = L"StartDoc falhou: " + winErrorText(GetLastError());
      DeleteDC(dc);
      return false;
    }
    docStarted = true;

    for (uint32_t i = 0; i < doc.PageCount(); i++) {
      PdfPage page = doc.GetPage(i);
      auto size = page.Size();                    // in DIPs (1/96 inch)
      double scale = std::min(pageW / (double)size.Width, pageH / (double)size.Height);
      int outW = std::max(1, (int)(size.Width * scale)), outH = std::max(1, (int)(size.Height * scale));

      PdfPageRenderOptions opts;
      opts.DestinationWidth((uint32_t)outW);
      opts.DestinationHeight((uint32_t)outH);
      opts.BitmapEncoderId(winrt::Windows::Graphics::Imaging::BitmapEncoder::BmpEncoderId());
      InMemoryRandomAccessStream bmp;
      page.RenderToStreamAsync(bmp, opts).get();
      std::vector<uint8_t> file = readAll(bmp);
      if (file.size() < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) throw hresult_error(E_FAIL, L"BMP inválido");
      auto* fh = (BITMAPFILEHEADER*)file.data();
      auto* bi = (BITMAPINFO*)(file.data() + sizeof(BITMAPFILEHEADER));
      const void* bits = file.data() + fh->bfOffBits;

      if (StartPage(dc) <= 0) throw hresult_error(HRESULT_FROM_WIN32(GetLastError()), L"StartPage falhou");
      SetStretchBltMode(dc, HALFTONE);
      SetBrushOrgEx(dc, 0, 0, nullptr);
      int srcW = bi->bmiHeader.biWidth, srcH = std::abs(bi->bmiHeader.biHeight);
      StretchDIBits(dc, 0, 0, outW, outH, 0, 0, srcW, srcH, bits, bi, DIB_RGB_COLORS, SRCCOPY);
      if (EndPage(dc) <= 0) throw hresult_error(HRESULT_FROM_WIN32(GetLastError()), L"EndPage falhou");
      pages++;
    }
    EndDoc(dc);
    DeleteDC(dc);
    return true;
  } catch (const hresult_error& e) {
    error = L"PDF: " + std::wstring(e.message().c_str());
  } catch (...) {
    error = L"PDF: erro desconhecido";
  }
  if (dc) {
    if (docStarted) AbortDoc(dc);
    DeleteDC(dc);
  }
  return false;
}
