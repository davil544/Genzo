//TODO: Finish the class here that holds the image, loads and saves it, converts it, etc.

#include "Codec.h"
#include <wx/wx.h>
#include <libheif/heif.h>

Codec::Codec()
{
	//init variables here
}

wxImage LoadImage(wxString path) {
	//loads image from path using libheif C code
	wxString input_filename = path;
	heif_context* ctx = heif_context_alloc();
	heif_context_read_from_file(ctx, input_filename, nullptr);

	// get a handle to the primary image
	heif_image_handle* handle;
	heif_context_get_primary_image_handle(ctx, &handle);

	// decode the image and convert colorspace to RGB, saved as 24bit interleaved
	heif_image* img;
	heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);

	int stride;
	const uint8_t* data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);

	// ... process data as needed ...

	// clean up resources
	heif_image_release(img);
	heif_image_handle_release(handle);
	heif_context_free(ctx);

	return wxImage();
}