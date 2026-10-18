#pragma once
#include <wx/image.h>
#include <wx/string.h>
#include "Metadata.h"

class Codec
{
public:
	static wxImage LoadHEIFImage(const wxString& filepath);
	// If `metadata` is non-null, its Exif and XMP are attached to the image through libheif
	// before the file is written (HEIF can't be patched after the fact the way JPEG/PNG can).
	static bool SaveHEIFImage(const wxImage& image, const wxString& filePath, int quality = 80, const MetadataBlobs* metadata = nullptr);
	static wxImage DecodeJP2(const unsigned char* data, size_t size);
	static wxImage LoadJP2(const wxString& filePath);
	static bool SaveJP2(const wxImage& img, const wxString& filePath, int quality);
	static wxImage LoadICNS(const wxString& filePath);
	static bool SaveICNS(const wxImage& img, const wxString& filePath);
};