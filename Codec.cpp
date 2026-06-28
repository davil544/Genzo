#include "Codec.h"
#include <wx/wx.h>
#include <wx/filename.h>
#include <libheif/heif.h>

//Codec::Codec(wxString filePath)
//{
	//init variables here
	//wxImage img;
    //wxFileName fileName(filePath);
//}

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

bool Codec::SaveHEIFImage(const wxImage& img, const wxString& filePath, int quality) {
    if (!img.IsOk()) {
        wxLogError("Invalid wxImage passed to HEIF encoder.");
        return false;
    }

    int width = img.GetWidth();
    int height = img.GetHeight();
    bool hasAlpha = img.HasAlpha();

    heif_image* h_image = nullptr;
    heif_chroma chroma = hasAlpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB;
    heif_error err = heif_image_create(width, height, heif_colorspace_RGB, chroma, &h_image);

    if (err.code != heif_error_Ok) {
        wxLogError("Failed to create HEIF image: %s", err.message);
        return false;
    }

    err = heif_image_add_plane(h_image, heif_channel_interleaved, width, height, hasAlpha ? 32 : 24);
    if (err.code != heif_error_Ok) {
        wxLogError("Failed to add plane to HEIF image: %s", err.message);
        heif_image_release(h_image);
        return false;
    }

    int stride;
    uint8_t* heif_data = heif_image_get_plane(h_image, heif_channel_interleaved, &stride);

    unsigned char* wx_rgb = img.GetData();
    unsigned char* wx_alpha = hasAlpha ? img.GetAlpha() : nullptr;

    for (int y = 0; y < height; ++y) {
        uint8_t* row = heif_data + (y * stride);
        for (int x = 0; x < width; ++x) {
            int wx_idx = (y * width + x);
            int rgb_idx = wx_idx * 3;
            int h_idx = x * (hasAlpha ? 4 : 3);

            row[h_idx] = wx_rgb[rgb_idx];     // R
            row[h_idx + 1] = wx_rgb[rgb_idx + 1]; // G
            row[h_idx + 2] = wx_rgb[rgb_idx + 2]; // B

            if (hasAlpha) {
                row[h_idx + 3] = wx_alpha[wx_idx]; // A
            }
        }
    }

    heif_context* ctx = heif_context_alloc();
    heif_encoder* encoder = nullptr;

    wxFileName fileName(filePath);
    fileName.Normalize(wxPATH_NORM_ALL & wxPATH_NORM_ENV_VARS);
    if (fileName.GetExt() == "avif") {
        err = heif_context_get_encoder_for_format(ctx, heif_compression_AV1, &encoder);
    }
    else {
        err = heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
    }

    bool success = false;

    if (err.code == heif_error_Ok) {
        heif_encoder_set_lossy_quality(encoder, quality);

        err = heif_context_encode_image(ctx, h_image, encoder, nullptr, nullptr);
        if (err.code == heif_error_Ok) {
            err = heif_context_write_to_file(ctx, filePath.utf8_str().data());
            if (err.code == heif_error_Ok) {
                success = true;
            }
            else {
                wxLogError("Failed to write HEIF file: %s", err.message);
            }
        }
        else {
            wxLogError("Failed to encode HEIF image: %s", err.message);
        }
        heif_encoder_release(encoder);
    }
    else {
        wxLogError("Failed to get HEVC encoder. Ensure libheif was compiled with an HEVC encoder (like x265).");
    }

    heif_context_free(ctx);
    heif_image_release(h_image);

    return success;
}