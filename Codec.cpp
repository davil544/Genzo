//TODO: Finish the class here that holds the image, loads and saves it, converts it, etc.

#include "Codec.h"
#include <wx/wx.h>
#include <libheif/heif.h>
#include <jpeglib.h>

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

void write_jpeg(const char* filename,
                const uint8_t* rgb,
                int width,
                int height,
                int stride)
{
    jpeg_compress_struct cinfo;
    jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    FILE* outfile = fopen(filename, "wb");
    jpeg_stdio_dest(&cinfo, outfile);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;          // RGB
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 90, TRUE);

    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = (JSAMPROW)(rgb + cinfo.next_scanline * stride);
        jpeg_write_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(outfile);
}