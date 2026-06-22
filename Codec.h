#pragma once
#include <wx/image.h>
#include <wx/string.h>

class Codec
{
public:
	
	//Codec(wxString filePath);
	static wxImage LoadHEIFImage(const wxString& filepath);
	static void write_jpeg(const char* filename,
		const uint8_t* rgb,
		int width,
		int height,
		int stride);
};