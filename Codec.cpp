#include "Codec.h"
#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/mstream.h>
#include <wx/wfstream.h>
#include <wx/image.h>
#include <libheif/heif.h>
#include <openjpeg.h>

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

wxImage Codec::LoadJP2(const wxString& filePath) {
    wxImage empty;

    wxFileInputStream jp2Stream(filePath);
    if (!jp2Stream.IsOk()) {
        wxLogError("Failed to open JP2 file: %s", filePath);
        return empty;
    }

    size_t dataSize = jp2Stream.GetLength();
    if (dataSize == 0) {
        wxLogError("JP2 file is empty: %s", filePath);
        return empty;
    }

    wxMemoryBuffer buffer(dataSize);
    if (jp2Stream.Read(buffer.GetData(), dataSize).LastRead() != dataSize) {
        wxLogError("Failed to read JP2 file into memory: %s", filePath);
        return empty;
    }

    unsigned char* data = static_cast<unsigned char*>(buffer.GetData());

    wxImage img = Codec::DecodeJP2(data, dataSize);
    if (!img.IsOk()) {
        wxLogError("OpenJPEG failed to decode JP2 file: %s", filePath);
        return empty;
    }

    return img;
}

wxImage Codec::DecodeJP2(const unsigned char* data, size_t size) {
    wxImage empty;

    opj_dparameters_t params;
    opj_set_default_decoder_parameters(&params);

    opj_stream_t* stream = opj_stream_create(size, true);
    if (!stream) return empty;

    opj_stream_set_user_data(stream, (void*)data, nullptr);
    opj_stream_set_user_data_length(stream, size);

    opj_stream_set_read_function(stream,
        [](void* p_buffer, size_t p_nb_bytes, void* p_user_data) -> size_t {
            unsigned char*& ptr = *(unsigned char**)p_user_data;
            memcpy(p_buffer, ptr, p_nb_bytes);
            ptr += p_nb_bytes;
            return p_nb_bytes;
        });

    unsigned char* readPtr = const_cast<unsigned char*>(data);
    opj_stream_set_user_data(stream, &readPtr, nullptr);

    opj_codec_t* codec = opj_create_decompress(OPJ_CODEC_JP2);
    if (!codec) {
        opj_stream_destroy(stream);
        return empty;
    }

    if (!opj_setup_decoder(codec, &params)) {
        opj_destroy_codec(codec);
        opj_stream_destroy(stream);
        return empty;
    }

    opj_image_t* jp2Image = nullptr;
    if (!opj_read_header(stream, codec, &jp2Image)) {
        opj_destroy_codec(codec);
        opj_stream_destroy(stream);
        return empty;
    }

    if (!opj_decode(codec, stream, jp2Image)) {
        opj_image_destroy(jp2Image);
        opj_destroy_codec(codec);
        opj_stream_destroy(stream);
        return empty;
    }

    int w = jp2Image->x1 - jp2Image->x0;
    int h = jp2Image->y1 - jp2Image->y0;

    bool hasAlpha = (jp2Image->numcomps == 4);

    wxImage img(w, h, hasAlpha);

    unsigned char* rgb = img.GetData();
    unsigned char* alpha = hasAlpha ? img.GetAlpha() : nullptr;

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            int idx = y * w + x;

            rgb[idx * 3 + 0] = jp2Image->comps[0].data[idx];
            rgb[idx * 3 + 1] = jp2Image->comps[1].data[idx];
            rgb[idx * 3 + 2] = jp2Image->comps[2].data[idx];

            if (hasAlpha)
                alpha[idx] = jp2Image->comps[3].data[idx];
        }
    }

    opj_image_destroy(jp2Image);
    opj_destroy_codec(codec);
    opj_stream_destroy(stream);

    return img;
}

wxImage Codec::LoadICNS(const wxString& filePath) {
    wxImage empty;

    wxFileInputStream in(filePath);
    if (!in.IsOk()) {
        wxLogError("Failed to open ICNS file: %s", filePath);
        return empty;
    }

    size_t fileSize = in.GetLength();
    if (fileSize < 8) {
        wxLogError("ICNS file too small: %s", filePath);
        return empty;
    }

    wxMemoryBuffer buffer(fileSize);
    if (in.Read(buffer.GetData(), fileSize).LastRead() != fileSize) {
        wxLogError("Failed to read ICNS file into memory: %s", filePath);
        return empty;
    }

    const unsigned char* data = static_cast<const unsigned char*>(buffer.GetData());

    if (fileSize < 8 || memcmp(data, "icns", 4) != 0) {
        wxLogError("Invalid ICNS header in file: %s", filePath);
        return empty;
    }

    auto ReadBE32 = [](const unsigned char* p) -> uint32_t {
        return (static_cast<uint32_t>(p[0]) << 24) |
            (static_cast<uint32_t>(p[1]) << 16) |
            (static_cast<uint32_t>(p[2]) << 8) |
            static_cast<uint32_t>(p[3]);
        };

    uint32_t totalSize = ReadBE32(data + 4);
    if (totalSize > fileSize) {
        totalSize = static_cast<uint32_t>(fileSize);
    }

    wxImage best;
    long bestPixels = 0;

    size_t offset = 8;
    while (offset + 8 <= totalSize)
    {
        const unsigned char* chunkHeader = data + offset;
        char type[5] = { 0 };
        memcpy(type, chunkHeader, 4);

        uint32_t chunkSize = ReadBE32(chunkHeader + 4);

        if (chunkSize < 8 || offset + chunkSize > totalSize) {
            wxLogError("Malformed ICNS chunk '%s' at offset %zu, stopping scan.", type, offset);
            break;
        }

        const unsigned char* chunkData = chunkHeader + 8;
        size_t chunkDataSize = chunkSize - 8;

        if (chunkDataSize > 8 &&
            chunkData[0] == 0x89 && chunkData[1] == 0x50 &&
            chunkData[2] == 0x4E && chunkData[3] == 0x47)
        {
            wxMemoryInputStream mem(chunkData, chunkDataSize);
            wxImage candidate;
            if (candidate.LoadFile(mem, wxBITMAP_TYPE_PNG) && candidate.IsOk())
            {
                long pixels = static_cast<long>(candidate.GetWidth()) *
                    static_cast<long>(candidate.GetHeight());
                if (pixels > bestPixels)
                {
                    bestPixels = pixels;
                    best = candidate;
                }
            }
        }

        offset += chunkSize;
    }

    if (!best.IsOk()) {
        wxLogError("No usable PNG image found in ICNS file: %s", filePath);
        return empty;
    }

    return best;
}

static void WriteBE32(wxFileOutputStream& out, uint32_t value)
{
    unsigned char b[4];
    b[0] = static_cast<unsigned char>((value >> 24) & 0xFF);
    b[1] = static_cast<unsigned char>((value >> 16) & 0xFF);
    b[2] = static_cast<unsigned char>((value >> 8) & 0xFF);
    b[3] = static_cast<unsigned char>(value & 0xFF);
    out.Write(b, 4);
}

static wxImage ScaleWithTransparentPadding(const wxImage& src, int targetSize) {
    wxImage working = src.Copy();

    if (!working.HasAlpha()) {
        working.InitAlpha();
    }

    int srcW = working.GetWidth();
    int srcH = working.GetHeight();

    double scale = std::min(
        static_cast<double>(targetSize) / srcW,
        static_cast<double>(targetSize) / srcH
    );

    int scaledW = std::max(1, static_cast<int>(std::round(srcW * scale)));
    int scaledH = std::max(1, static_cast<int>(std::round(srcH * scale)));

    wxImage scaled = working.Scale(scaledW, scaledH, wxIMAGE_QUALITY_HIGH);
    if (!scaled.HasAlpha()) {
        scaled.InitAlpha();
    }

    wxImage padded(targetSize, targetSize);
    padded.InitAlpha();

    unsigned char* dstRGB = padded.GetData();
    unsigned char* dstAlpha = padded.GetAlpha();

    memset(dstRGB, 0, targetSize * targetSize * 3);
    memset(dstAlpha, 0, targetSize * targetSize);

    unsigned char* srcRGB = scaled.GetData();
    unsigned char* srcAlpha = scaled.GetAlpha();

    int offsetX = (targetSize - scaledW) / 2;
    int offsetY = (targetSize - scaledH) / 2;

    for (int y = 0; y < scaledH; ++y)
    {
        int dstY = offsetY + y;
        if (dstY < 0 || dstY >= targetSize) continue;

        unsigned char* dstRowRGB = dstRGB + (dstY * targetSize + offsetX) * 3;
        const unsigned char* srcRowRGB = srcRGB + (y * scaledW) * 3;
        memcpy(dstRowRGB, srcRowRGB, scaledW * 3);

        unsigned char* dstRowAlpha = dstAlpha + (dstY * targetSize + offsetX);
        const unsigned char* srcRowAlpha = srcAlpha + (y * scaledW);
        memcpy(dstRowAlpha, srcRowAlpha, scaledW);
    }

    return padded;
}

bool Codec::SaveICNS(const wxImage& img, const wxString& filePath)
{
    if (!img.IsOk()) {
        wxLogError("Invalid wxImage passed to SaveICNS.");
        return false;
    }

    struct IconSpec {
        const char* type;
        int size;
    };

    const IconSpec icons[] = {
        {"icp4", 16},
        {"icp5", 32},
        {"icp6", 64},
        {"ic07", 128},
        {"ic08", 256},
        {"ic09", 512},
        {"ic10", 1024},
        {"ic11", 32},
        {"ic12", 64},
        {"ic13", 256},
        {"ic14", 512},
    };

    wxFileOutputStream out(filePath);
    if (!out.IsOk()) {
        wxLogError("Failed to open ICNS output file: %s", filePath);
        return false;
    }

    char magic[4] = { 'i','c','n','s' };
    out.Write(magic, 4);

    WriteBE32(out, 0);

    uint32_t totalSize = 8;

    for (const auto& icon : icons)
    {
        wxImage scaled = ScaleWithTransparentPadding(img, icon.size);

        wxMemoryOutputStream pngStream;
        if (!scaled.SaveFile(pngStream, wxBITMAP_TYPE_PNG)) {
            wxLogError("Failed to encode PNG for ICNS block: %s", icon.type);
            return false;
        }

        size_t pngSize = pngStream.GetSize();
        if (pngSize == 0) {
            wxLogError("Empty PNG stream for ICNS block: %s", icon.type);
            return false;
        }

        wxStreamBuffer* buf = pngStream.GetOutputStreamBuffer();
        buf->Seek(0, wxFromStart);

        wxMemoryBuffer pngData(pngSize);
        buf->Read(pngData.GetWriteBuf(pngSize), pngSize);
        pngData.UngetWriteBuf(pngSize);

        char type[4];
        memcpy(type, icon.type, 4);
        out.Write(type, 4);

        uint32_t blockSize = 8 + static_cast<uint32_t>(pngSize);
        WriteBE32(out, blockSize);

        out.Write(pngData.GetData(), pngSize);

        totalSize += blockSize;
    }

    out.SeekO(4, wxFromStart);
    WriteBE32(out, totalSize);

    wxLogMessage("ICNS file successfully written: %s", filePath);
    return true;
}