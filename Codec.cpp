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
        wxLogError("Failed to open JPEG 2000 file: %s", filePath);
        return empty;
    }

    size_t dataSize = jp2Stream.GetLength();
    if (dataSize == 0) {
        wxLogError("JPEG 2000 file is empty: %s", filePath);
        return empty;
    }

    wxMemoryBuffer buffer(dataSize);
    if (jp2Stream.Read(buffer.GetData(), dataSize).LastRead() != dataSize) {
        wxLogError("Failed to read JPEG 2000 file into memory: %s", filePath);
        return empty;
    }

    unsigned char* data = static_cast<unsigned char*>(buffer.GetData());

    wxImage img = Codec::DecodeJP2(data, dataSize);
    if (!img.IsOk()) {
        wxLogError("OpenJPEG failed to decode JPEG 2000 file: %s", filePath);
        return empty;
    }

    return img;
}

static OPJ_CODEC_FORMAT DetectJP2Format(const unsigned char* data, size_t size) {
    if (size >= 12 &&
        data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x0C &&
        data[4] == 0x6A && data[5] == 0x50 && data[6] == 0x20 && data[7] == 0x20)
    {
        return OPJ_CODEC_JP2; // .jp2 / .jpf / .jpx (shared box container)
    }

    if (size >= 4 &&
        data[0] == 0xFF && data[1] == 0x4F &&
        data[2] == 0xFF && data[3] == 0x51)
    {
        return OPJ_CODEC_J2K; // .j2c / .j2k raw codestream
    }

    return OPJ_CODEC_UNKNOWN;
}

wxImage Codec::DecodeJP2(const unsigned char* data, size_t size) {
    wxImage empty;

    OPJ_CODEC_FORMAT format = DetectJP2Format(data, size);
    if (format == OPJ_CODEC_UNKNOWN) {
        wxLogError("Unrecognized JPEG 2000 stream: no JP2 box or J2K codestream signature found.");
        return empty;
    }

    opj_dparameters_t params;
    opj_set_default_decoder_parameters(&params);

    opj_stream_t* stream = opj_stream_create(size, true);
    if (!stream) return empty;

    unsigned char* readPtr = const_cast<unsigned char*>(data);
    const unsigned char* dataEnd = data + size;

    opj_stream_set_read_function(stream,
        [](void* p_buffer, size_t p_nb_bytes, void* p_user_data) -> size_t {
            auto* ctx = static_cast<std::pair<unsigned char*, const unsigned char*>*>(p_user_data);
            size_t remaining = static_cast<size_t>(ctx->second - ctx->first);
            size_t toRead = std::min(p_nb_bytes, remaining);
            if (toRead == 0) return static_cast<size_t>(-1);
            memcpy(p_buffer, ctx->first, toRead);
            ctx->first += toRead;
            return toRead;
        });

    std::pair<unsigned char*, const unsigned char*> readCtx(readPtr, dataEnd);
    opj_stream_set_user_data(stream, &readCtx, nullptr);
    opj_stream_set_user_data_length(stream, size);

    opj_codec_t* codec = opj_create_decompress(format);
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

// Builds an opj_image_t (3 or 4 x 8-bit components) from a wxImage.
// Caller owns the returned image and must opj_image_destroy() it.
static opj_image_t* CreateOPJImageFromWx(const wxImage& img) {
    int width = img.GetWidth();
    int height = img.GetHeight();
    bool hasAlpha = img.HasAlpha();
    int numComps = hasAlpha ? 4 : 3;

    opj_image_cmptparm_t cmptparm[4];
    memset(cmptparm, 0, sizeof(cmptparm));
    for (int i = 0; i < numComps; ++i) {
        cmptparm[i].dx = 1;
        cmptparm[i].dy = 1;
        cmptparm[i].w = width;
        cmptparm[i].h = height;
        cmptparm[i].x0 = 0;
        cmptparm[i].y0 = 0;
        cmptparm[i].prec = 8;
        cmptparm[i].bpp = 8;
        cmptparm[i].sgnd = 0;
    }

    opj_image_t* image = opj_image_create(numComps, cmptparm, OPJ_CLRSPC_SRGB);
    if (!image) return nullptr;

    image->x0 = 0;
    image->y0 = 0;
    image->x1 = width;
    image->y1 = height;

    const unsigned char* rgb = img.GetData();
    const unsigned char* alpha = hasAlpha ? img.GetAlpha() : nullptr;

    int pixelCount = width * height;
    for (int i = 0; i < pixelCount; ++i) {
        image->comps[0].data[i] = rgb[i * 3 + 0];
        image->comps[1].data[i] = rgb[i * 3 + 1];
        image->comps[2].data[i] = rgb[i * 3 + 2];
        if (hasAlpha) {
            image->comps[3].data[i] = alpha[i];
        }
    }

    return image;
}

// Writes a wxImage as JP2 (.jp2/.jpf, boxed format) or raw codestream
// (.j2k/.j2c), chosen by the output file's extension. quality is 1-100;
// 100 requests lossless encoding, anything lower is treated as a
// compression-ratio target (lower quality = smaller/lossier file).
bool Codec::SaveJP2(const wxImage& img, const wxString& filePath, int quality) {
    if (!img.IsOk()) {
        wxLogError("Invalid wxImage passed to SaveJP2.");
        return false;
    }

    quality = std::clamp(quality, 1, 100);

    wxFileName fileName(filePath);
    fileName.Normalize(wxPATH_NORM_ALL & wxPATH_NORM_ENV_VARS);
    wxString ext = fileName.GetExt().Lower();

    OPJ_CODEC_FORMAT format =
        (ext == "j2k" || ext == "j2c") ? OPJ_CODEC_J2K : OPJ_CODEC_JP2;

    opj_image_t* image = CreateOPJImageFromWx(img);
    if (!image) {
        wxLogError("Failed to build OpenJPEG image from wxImage.");
        return false;
    }

    opj_cparameters_t params;
    opj_set_default_encoder_parameters(&params);

    // Single quality layer. quality == 100 -> lossless (reversible 5-3
    // wavelet, no rate cap). Otherwise -> lossy with a rate target derived
    // from quality (roughly: lower quality => higher compression ratio).
    params.tcp_numlayers = 1;
    params.cp_disto_alloc = 1;

    if (quality >= 100) {
        params.irreversible = 0;
        params.tcp_rates[0] = 0; // 0 = lossless in OpenJPEG's convention
    }
    else {
        params.irreversible = 1; // 9-7 wavelet, needed for lossy
        // Map 1-99 -> a compression ratio roughly between 40:1 and ~1.1:1.
        double ratio = 1.0 + (100 - quality) * 0.4;
        params.tcp_rates[0] = static_cast<float>(ratio);
    }

    opj_codec_t* codec = opj_create_compress(format);
    if (!codec) {
        wxLogError("Failed to create OpenJPEG encoder.");
        opj_image_destroy(image);
        return false;
    }

    if (!opj_setup_encoder(codec, &params, image)) {
        wxLogError("Failed to set up OpenJPEG encoder.");
        opj_destroy_codec(codec);
        opj_image_destroy(image);
        return false;
    }

    opj_stream_t* stream = opj_stream_create_default_file_stream(
        filePath.utf8_str(), OPJ_FALSE /* write stream */);
    if (!stream) {
        wxLogError("Failed to open output stream for: %s", filePath);
        opj_destroy_codec(codec);
        opj_image_destroy(image);
        return false;
    }

    bool success = false;
    if (!opj_start_compress(codec, image, stream)) {
        wxLogError("opj_start_compress failed for: %s", filePath);
    }
    else if (!opj_encode(codec, stream)) {
        wxLogError("opj_encode failed for: %s", filePath);
    }
    else if (!opj_end_compress(codec, stream)) {
        wxLogError("opj_end_compress failed for: %s", filePath);
    }
    else {
        success = true;
    }

    opj_stream_destroy(stream);
    opj_destroy_codec(codec);
    opj_image_destroy(image);

    if (success) {
        wxLogMessage("JPEG 2000 file successfully written: %s", filePath);
    }

    return success;
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
    while (offset + 8 <= totalSize) {
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

    for (int y = 0; y < scaledH; ++y) {
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

    for (const auto& icon : icons) {
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