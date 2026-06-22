//TODO: Finish the class here that holds the image, loads and saves it, converts it, etc.

#include "Codec.h"
#include <wx/wx.h>
#include <wx/filename.h>
#include <libheif/heif.h>
#include <jpeglib.h>

//Codec::Codec(wxString filePath)
//{
	//init variables here
	//wxImage img;
    //wxFileName fileName(filePath);
//}

/*
wxImage Codec::LoadHEIFImage(wxString path) {
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
*/

wxImage Codec::LoadHEIFImage(const wxString& filepath) {
    wxImage emptyImage; 

    heif_context* ctx = heif_context_alloc();
    heif_error err = heif_context_read_from_file(ctx, filepath.utf8_str(), nullptr);
    if (err.code != heif_error_Ok) {
        wxLogError("libheif failed to read file: %s", err.message);
        heif_context_free(ctx);
        return emptyImage;
    }

    heif_image_handle* handle;
    err = heif_context_get_primary_image_handle(ctx, &handle);
    if (err.code != heif_error_Ok) {
        wxLogError("libheif failed to get primary image handle: %s", err.message);
        heif_context_free(ctx);
        return emptyImage;
    }

    int width = heif_image_handle_get_width(handle);
    int height = heif_image_handle_get_height(handle);

    heif_image* img;
    err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);
    if (err.code != heif_error_Ok) {
        wxLogError("libheif failed to decode image: %s", err.message);
        heif_image_handle_release(handle);
        heif_context_free(ctx);
        return emptyImage;
    }

    int stride;
    const uint8_t* heif_data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);

    unsigned char* wxData = (unsigned char*)malloc(width * height * 3);
    if (!wxData) {
        wxLogError("Failed to allocate memory for wxImage pixel data.");
        heif_image_release(img);
        heif_image_handle_release(handle);
        heif_context_free(ctx);
        return emptyImage;
    }

    for (int y = 0; y < height; ++y) {
        memcpy(wxData + (y * width * 3), heif_data + (y * stride), width * 3);
    }

    heif_image_release(img);
    heif_image_handle_release(handle);
    heif_context_free(ctx);

    return wxImage(width, height, wxData);
}

void Codec::write_jpeg(const char* filename,
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