#pragma once
#include <wx/image.h>
#include <wx/string.h>

class Codec
{
public:
	
	//Codec(wxString filePath);
	static wxImage LoadHEIFImage(const wxString& filepath);
	static bool SaveHEIFImage(const wxImage& image, const wxString& filePath, int quality = 80);
};