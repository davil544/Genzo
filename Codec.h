#pragma once
#include <wx/image.h>
#include <wx/string.h>

class Codec
{
public:
	static wxImage LoadHEIFImage(const wxString& filepath);
	static bool SaveHEIFImage(const wxImage& image, const wxString& filePath, int quality = 80);
	static wxImage DecodeJP2(const unsigned char* data, size_t size);
	static wxImage LoadJP2(const wxString& filePath);
	static wxImage LoadICNS(const wxString& filePath);
	static bool SaveICNS(const wxImage& img, const wxString& filePath);
};