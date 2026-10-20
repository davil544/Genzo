#include "Codec.h"
#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/mstream.h>
#include <wx/wfstream.h>
#include <wx/image.h>
#include <libheif/heif.h>
#include <openjpeg.h>
#include <gif_lib.h>
#include <wx/stream.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cmath>
#include <webp/decode.h>
#include <webp/demux.h>
#include <webp/encode.h>
#include <webp/mux.h>

// Animated AVIF needs libheif's image-sequence API (heif_sequences.h, libheif 1.20+). heif.h doesn't pull that
// header in on every build, so it is included explicitly when it exists.
// Without it the AVIF writer still works, it just saves the first frame as a still image, and the
// reader loads a single frame.
#if defined(__has_include)
#if __has_include(<libheif/heif_sequences.h>)
#include <libheif/heif_sequences.h>
#define GENZO_HEIF_SEQUENCES 1
#endif
#endif

// Progress lines for the GIF writer; they show up in the Visual Studio Output window in Debug builds.
#define GIF_TRACE(...) wxLogDebug(__VA_ARGS__)

// ---- giflib encode/decode core. Plain C++ + giflib only (no wxWidgets) ----
#include <chrono>
#ifndef GIF_TRACE
#define GIF_TRACE(...) ((void)0)
#endif
namespace {

    long long GifMs(const std::chrono::steady_clock::time_point& t0) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    }

    // Median-cut quantizer (giflib's own GifQuantizeBuffer isn't declared in the vcpkg build's header).
    // Input: the R/G/B planes of the opaque pixels. Output: up to `maxColors` palette entries and
    // one palette index per input pixel.
    struct GifBin { uint32_t count = 0; uint64_t r = 0, g = 0, b = 0; };
    struct GifEntry { int key; uint32_t count; };
    struct GifBox { size_t lo, hi; uint64_t pop; };

    void GifQuantize(const std::vector<GifByteType>& R, const std::vector<GifByteType>& G, const std::vector<GifByteType>& B,
        int maxColors, std::vector<GifColorType>& palette, std::vector<GifByteType>& indices) {
        const size_t n = R.size();
        palette.clear();
        indices.assign(n, 0);
        if (n == 0) return;

        auto keyOf = [](int r, int g, int b) { return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3); }; // 5 bits/channel

        std::vector<GifBin> hist(32768);
        for (size_t i = 0; i < n; ++i) {
            GifBin& bin = hist[keyOf(R[i], G[i], B[i])];
            bin.count++; bin.r += R[i]; bin.g += G[i]; bin.b += B[i];
        }
        std::vector<GifEntry> entries;
        for (int k = 0; k < 32768; ++k)
            if (hist[k].count) entries.push_back({ k, hist[k].count });

        std::vector<GifBox> boxes{ { 0, entries.size(), n } };
        while (static_cast<int>(boxes.size()) < maxColors) {
            int best = -1; uint64_t bestPop = 0;
            for (size_t i = 0; i < boxes.size(); ++i)
                if (boxes[i].hi - boxes[i].lo > 1 && boxes[i].pop > bestPop) { best = static_cast<int>(i); bestPop = boxes[i].pop; }
            if (best < 0) break;

            const GifBox b = boxes[best];
            int mn[3] = { 31, 31, 31 }, mx[3] = { 0, 0, 0 };
            for (size_t k = b.lo; k < b.hi; ++k) {
                const int c[3] = { entries[k].key >> 10, (entries[k].key >> 5) & 31, entries[k].key & 31 };
                for (int j = 0; j < 3; ++j) { mn[j] = std::min(mn[j], c[j]); mx[j] = std::max(mx[j], c[j]); }
            }
            int ch = 0;
            for (int j = 1; j < 3; ++j) if (mx[j] - mn[j] > mx[ch] - mn[ch]) ch = j;
            const int shift = (ch == 0) ? 10 : (ch == 1 ? 5 : 0);
            std::sort(entries.begin() + b.lo, entries.begin() + b.hi,
                [shift](const GifEntry& a, const GifEntry& c) { return ((a.key >> shift) & 31) < ((c.key >> shift) & 31); });

            uint64_t acc = 0; const uint64_t half = b.pop / 2; size_t mid = b.lo + 1;
            for (size_t k = b.lo; k + 1 < b.hi; ++k) {
                acc += entries[k].count; mid = k + 1;
                if (acc >= half) break;
            }
            boxes[best] = { b.lo, mid, acc };
            boxes.push_back({ mid, b.hi, b.pop - acc });
        }

        for (const GifBox& b : boxes) {
            uint64_t r = 0, g = 0, bl = 0, cnt = 0;
            for (size_t k = b.lo; k < b.hi; ++k) {
                const GifBin& bin = hist[entries[k].key];
                r += bin.r; g += bin.g; bl += bin.b; cnt += bin.count;
            }
            GifColorType c;
            c.Red = static_cast<GifByteType>((r + cnt / 2) / cnt);
            c.Green = static_cast<GifByteType>((g + cnt / 2) / cnt);
            c.Blue = static_cast<GifByteType>((bl + cnt / 2) / cnt);
            palette.push_back(c);
        }

        // Nearest palette entry per 5-bit colour, cached so each distinct colour is only searched once.
        std::vector<int16_t> cache(32768, -1);
        for (size_t i = 0; i < n; ++i) {
            const int key = keyOf(R[i], G[i], B[i]);
            if (cache[key] < 0) {
                int bestI = 0; long bestD = 1L << 30;
                for (size_t p = 0; p < palette.size(); ++p) {
                    const long dr = R[i] - palette[p].Red, dg = G[i] - palette[p].Green, db = B[i] - palette[p].Blue;
                    const long d = dr * dr + dg * dg + db * db;
                    if (d < bestD) { bestD = d; bestI = static_cast<int>(p); }
                }
                cache[key] = static_cast<int16_t>(bestI);
            }
            indices[i] = static_cast<GifByteType>(cache[key]);
        }
    }

    // ---- Helpers shared by the animated formats (GIF, WebP, AVIF) ----

    // Returns `image` as-is if it is already w x h, otherwise a scaled copy. All frames of an animation share one canvas.
    wxImage FitToCanvas(const wxImage& image, int w, int h) {
        if (image.GetWidth() == w && image.GetHeight() == h) return image;
        return image.Scale(w, h, wxIMAGE_QUALITY_HIGH);
    }

    // wxImage -> interleaved RGBA. Alpha comes from the alpha channel, or from the mask colour when there is no alpha.
    void ImageToRGBA(const wxImage& image, std::vector<unsigned char>& rgba) {
        const size_t total = static_cast<size_t>(image.GetWidth()) * static_cast<size_t>(image.GetHeight());
        const unsigned char* rgb = image.GetData();
        const unsigned char* alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;
        const bool useMask = !alpha && image.HasMask();
        const unsigned char mr = useMask ? image.GetMaskRed() : 0, mg = useMask ? image.GetMaskGreen() : 0, mb = useMask ? image.GetMaskBlue() : 0;

        rgba.resize(total * 4);
        for (size_t i = 0; i < total; ++i) {
            rgba[4 * i] = rgb[3 * i];
            rgba[4 * i + 1] = rgb[3 * i + 1];
            rgba[4 * i + 2] = rgb[3 * i + 2];
            rgba[4 * i + 3] = alpha ? alpha[i] : ((useMask && rgb[3 * i] == mr && rgb[3 * i + 1] == mg && rgb[3 * i + 2] == mb) ? 0 : 255);
        }
    }

    // Interleaved RGBA -> wxImage. The alpha channel is only attached if some pixel is actually translucent.
    wxImage ImageFromRGBA(int w, int h, const unsigned char* rgba, size_t stride) {
        wxImage image(w, h, false);
        if (!image.IsOk()) return image;

        unsigned char* rgb = image.GetData();
        bool opaque = true;
        for (int y = 0; y < h; ++y) {
            const unsigned char* src = rgba + static_cast<size_t>(y) * stride;
            for (int x = 0; x < w; ++x) {
                *rgb++ = src[4 * x];
                *rgb++ = src[4 * x + 1];
                *rgb++ = src[4 * x + 2];
                if (src[4 * x + 3] != 255) opaque = false;
            }
        }
        if (!opaque) {
            image.InitAlpha();
            unsigned char* a = image.GetAlpha();
            for (int y = 0; y < h; ++y) {
                const unsigned char* src = rgba + static_cast<size_t>(y) * stride;
                for (int x = 0; x < w; ++x) *a++ = src[4 * x + 3];
            }
        }
        return image;
    }

    // Encodes a single-frame GIF. `rgb` is w*h*3. `alpha` (w*h) or `maskRgb` (3 bytes) mark transparent pixels.
    // Quantization, palette handling and LZW compression are all done by giflib.
    // In an animation this runs once per frame, between the screen descriptor and the trailer, and each frame gets its own palette.
    bool GifPutFrame(GifFileType* gif, const unsigned char* rgb, const unsigned char* alpha, const unsigned char* maskRgb,
        int w, int h, int delayMs, bool animated, const std::chrono::steady_clock::time_point& t0, wxString* error) {
        auto fail = [&](const wxString& msg) { if (error) *error = msg; return false; };
        if (!rgb || w <= 0 || h <= 0) return fail("Empty image.");
        const size_t total = static_cast<size_t>(w) * static_cast<size_t>(h);

        auto transparentAt = [&](size_t i) {
            if (alpha) return alpha[i] < 128;
            if (maskRgb) return rgb[3 * i] == maskRgb[0] && rgb[3 * i + 1] == maskRgb[1] && rgb[3 * i + 2] == maskRgb[2];
            return false;
            };

        // Split the opaque pixels into R/G/B planes. They're handed to giflib as one long row, so
        // transparent pixels never influence the palette.
        std::vector<GifByteType> R, G, B;
        R.reserve(total); G.reserve(total); B.reserve(total);
        bool hasTrans = false;
        for (size_t i = 0; i < total; ++i) {
            if (transparentAt(i)) { hasTrans = true; continue; }
            R.push_back(rgb[3 * i]); G.push_back(rgb[3 * i + 1]); B.push_back(rgb[3 * i + 2]);
        }

        GIF_TRACE("[GIF] split planes: %d opaque pixels, transparent=%d (+%lld ms)", static_cast<int>(R.size()), hasTrans ? 1 : 0, GifMs(t0));
        // One palette slot is kept back for the transparent colour.
        std::vector<GifColorType> quantPalette;
        std::vector<GifByteType> quantIdx;
        GifQuantize(R, G, B, hasTrans ? 255 : 256, quantPalette, quantIdx);
        const int colorCount = static_cast<int>(quantPalette.size());
        GIF_TRACE("[GIF] quantized to %d colours (+%lld ms)", colorCount, GifMs(t0));

        const int usedColors = colorCount + (hasTrans ? 1 : 0);
        int bits = 1;
        while ((1 << bits) < usedColors) ++bits;
        ColorMapObject* map = GifMakeMapObject(1 << bits, nullptr);
        if (!map) return fail("Out of memory.");
        for (int i = 0; i < colorCount; ++i) map->Colors[i] = quantPalette[i];
        const int transIdx = colorCount;

        bool ok = true;
        if (hasTrans || animated) {
            GraphicsControlBlock gcb;
            // Every frame is a full-canvas image, so a frame with holes has to be wiped before the next one is drawn.
            gcb.DisposalMode = (animated && hasTrans) ? DISPOSE_BACKGROUND : DISPOSAL_UNSPECIFIED;
            gcb.UserInputFlag = false;
            gcb.DelayTime = animated ? std::min(65535, (delayMs + 5) / 10) : 0; // GIF delays are in 1/100 s
            gcb.TransparentColor = hasTrans ? transIdx : NO_TRANSPARENT_COLOR;
            GifByteType ext[4];
            EGifGCBToExtension(&gcb, ext);
            ok = EGifPutExtension(gif, GRAPHICS_EXT_FUNC_CODE, 4, ext) == GIF_OK;
        }
        ok = ok && EGifPutImageDesc(gif, 0, 0, w, h, false, map) == GIF_OK; // giflib copies the palette

        std::vector<GifByteType> row(static_cast<size_t>(w));
        size_t q = 0;
        for (int y = 0; ok && y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t i = static_cast<size_t>(y) * w + x;
                row[x] = transparentAt(i) ? static_cast<GifByteType>(transIdx) : quantIdx[q++];
            }
            ok = EGifPutLine(gif, row.data(), w) == GIF_OK;
        }
        GifFreeMapObject(map);
        GIF_TRACE("[GIF] frame written, ok=%d (+%lld ms)", ok ? 1 : 0, GifMs(t0));
        return ok ? true : fail(GifErrorString(gif->Error));
    }

    // Encodes every frame of `anim` into one GIF (a single frame gives a plain still GIF).
    bool GifEncode(const AnimatedImage& anim, OutputFunc write, void* userData, wxString* error) {
        auto fail = [&](const wxString& msg) { if (error) *error = msg; return false; };
        const auto t0 = std::chrono::steady_clock::now();
        if (!anim.IsOk()) return fail("Empty image.");
        const int w = anim.frames[0].image.GetWidth(), h = anim.frames[0].image.GetHeight();
        if (w > 65535 || h > 65535) return fail("GIF images can be at most 65535 x 65535 pixels.");
        const bool animated = anim.IsAnimated();
        GIF_TRACE("[GIF] encode start: %d x %d, %d frame(s)", w, h, static_cast<int>(anim.frames.size()));

        int err = 0;
        GifFileType* gif = EGifOpen(userData, write, &err);
        GIF_TRACE("[GIF] EGifOpen -> %s (+%lld ms)", gif ? "ok" : "FAILED", GifMs(t0));
        if (!gif) return fail(GifErrorString(err));

        bool ok = true;
        EGifSetGifVersion(gif, true); // 89a, needed for the transparency extension
        ok = ok && EGifPutScreenDesc(gif, w, h, 8, 0, nullptr) == GIF_OK; // every frame carries its own palette

        // The de-facto NETSCAPE2.0 looping extension. Without it a GIF plays once. The stored value is the number of repeats, 0 = forever.
        if (ok && animated && anim.loopCount != 1) {
            const int repeats = anim.loopCount <= 0 ? 0 : std::min(65535, anim.loopCount - 1);
            const GifByteType loop[3] = { 1, static_cast<GifByteType>(repeats & 0xFF), static_cast<GifByteType>(repeats >> 8) };
            ok = EGifPutExtensionLeader(gif, APPLICATION_EXT_FUNC_CODE) == GIF_OK
                && EGifPutExtensionBlock(gif, 11, "NETSCAPE2.0") == GIF_OK
                && EGifPutExtensionBlock(gif, 3, loop) == GIF_OK
                && EGifPutExtensionTrailer(gif) == GIF_OK;
        }

        wxString frameError;
        for (size_t f = 0; ok && f < anim.frames.size(); ++f) {
            const wxImage image = FitToCanvas(anim.frames[f].image, w, h);
            if (!image.IsOk()) { ok = false; frameError = "A frame of the animation is invalid."; break; }

            unsigned char mask[3] = { 0, 0, 0 };
            const bool useMask = !image.HasAlpha() && image.HasMask();
            if (useMask) { mask[0] = image.GetMaskRed(); mask[1] = image.GetMaskGreen(); mask[2] = image.GetMaskBlue(); }

            ok = GifPutFrame(gif, image.GetData(), image.HasAlpha() ? image.GetAlpha() : nullptr, useMask ? mask : nullptr,
                w, h, anim.frames[f].delayMs, animated, t0, &frameError);
        }

        const int lastError = gif->Error;
        int closeErr = 0;
        GIF_TRACE("[GIF] frames done, closing (+%lld ms)", GifMs(t0));
        const bool closed = EGifCloseFile(gif, &closeErr) == GIF_OK; // also frees `gif`
        GIF_TRACE("[GIF] closed, ok=%d (+%lld ms)", closed ? 1 : 0, GifMs(t0));
        if (!ok) return fail(frameError.empty() ? wxString(GifErrorString(lastError)) : frameError);
        if (!closed) return fail(GifErrorString(closeErr));
        return true;
    }

    struct GifDecoded {
        struct Frame {
            std::vector<unsigned char> rgb;
            std::vector<unsigned char> alpha; // empty if the frame is fully opaque
            int delayMs = 100;
        };
        int width = 0, height = 0, frameCount = 0;
        int loopCount = 1; // times the animation plays, 0 = forever (see AnimatedImage::loopCount)
        std::vector<Frame> frames;
    };

    // Reads the NETSCAPE2.0 repeat count. giflib files the extension under the first image.
    int GifReadLoopCount(const GifFileType* gif) {
        auto scan = [](const ExtensionBlock* blocks, int count) {
            for (int i = 0; i + 1 < count; ++i) {
                const ExtensionBlock& a = blocks[i];
                const ExtensionBlock& b = blocks[i + 1];
                if (a.Function == APPLICATION_EXT_FUNC_CODE && a.ByteCount >= 11 && std::memcmp(a.Bytes, "NETSCAPE2.0", 11) == 0
                    && b.Function == CONTINUE_EXT_FUNC_CODE && b.ByteCount >= 3 && b.Bytes[0] == 1) {
                    const int repeats = b.Bytes[1] | (b.Bytes[2] << 8);
                    return repeats == 0 ? 0 : repeats + 1;
                }
            }
            return -1;
            };
        int loops = gif->ImageCount > 0 ? scan(gif->SavedImages[0].ExtensionBlocks, gif->SavedImages[0].ExtensionBlockCount) : -1;
        if (loops < 0) loops = scan(gif->ExtensionBlocks, gif->ExtensionBlockCount);
        return loops < 0 ? 1 : loops;
    }

    // Decodes frame `frameIndex` (0 = first) onto a transparent canvas the size of the GIF's logical screen.
    // Now decodes the first `maxFrames` frames (-1 = all of them) instead of one. Each one is a full, composited
    // canvas, so frame disposal is already applied.
    bool GifDecode(InputFunc read, void* userData, int maxFrames, GifDecoded& out, wxString* error) {
        auto fail = [&](const wxString& msg) { if (error) *error = msg; return false; };

        int err = 0;
        GifFileType* gif = DGifOpen(userData, read, &err);
        if (!gif) return fail(GifErrorString(err));
        if (DGifSlurp(gif) != GIF_OK) {
            const int e = gif->Error;
            int ce = 0; DGifCloseFile(gif, &ce);
            return fail(GifErrorString(e));
        }

        bool ok = false;
        do {
            out.frameCount = gif->ImageCount;
            if (gif->ImageCount < 1) { fail("The GIF contains no images."); break; }
            if (maxFrames > gif->ImageCount) { fail("GIF frame index out of range."); break; }
            const int wanted = maxFrames < 0 ? gif->ImageCount : maxFrames;

            int sw = gif->SWidth, sh = gif->SHeight;
            if (sw <= 0 || sh <= 0) {
                const GifImageDesc& d0 = gif->SavedImages[0].ImageDesc;
                sw = d0.Left + d0.Width; sh = d0.Top + d0.Height;
            }
            if (sw <= 0 || sh <= 0) { fail("The GIF has an invalid size."); break; }

            out.width = sw; out.height = sh;
            out.loopCount = GifReadLoopCount(gif);
            out.frames.clear();

            std::vector<unsigned char> canvas(static_cast<size_t>(sw) * sh * 4, 0), beforeFrame;
            bool bad = false;
            for (int i = 0; i < wanted && !bad; ++i) {
                const SavedImage& frame = gif->SavedImages[i];
                const GifImageDesc& d = frame.ImageDesc;
                const ColorMapObject* cmap = d.ColorMap ? d.ColorMap : gif->SColorMap;
                if (!cmap || !frame.RasterBits) { fail("The GIF has no colour map."); bad = true; break; }

                GraphicsControlBlock gcb;
                gcb.DisposalMode = DISPOSAL_UNSPECIFIED; gcb.DelayTime = 0; gcb.TransparentColor = NO_TRANSPARENT_COLOR;
                if (DGifSavedExtensionToGCB(gif, i, &gcb) != GIF_OK) gcb.TransparentColor = NO_TRANSPARENT_COLOR;

                if (gcb.DisposalMode == DISPOSE_PREVIOUS) beforeFrame = canvas;

                // DGifSlurp already de-interlaces the raster bits, so rows can be read in order.
                for (int sr = 0; sr < d.Height; ++sr) {
                    const int y = d.Top + sr;
                    if (y < 0 || y >= sh) continue;
                    for (int sx = 0; sx < d.Width; ++sx) {
                        const int x = d.Left + sx;
                        if (x < 0 || x >= sw) continue;
                        const int ci = frame.RasterBits[static_cast<size_t>(sr) * d.Width + sx];
                        // A transparent index leaves the canvas as it was: still empty on the first frame, an earlier frame's pixel after that.
                        if (ci == gcb.TransparentColor) continue; // stays alpha 0
                        if (ci >= cmap->ColorCount) continue;
                        unsigned char* px = &canvas[(static_cast<size_t>(y) * sw + x) * 4];
                        px[0] = cmap->Colors[ci].Red; px[1] = cmap->Colors[ci].Green; px[2] = cmap->Colors[ci].Blue; px[3] = 255;
                    }
                }

                // Snapshot the canvas as this frame.
                GifDecoded::Frame snap;
                const size_t pixels = static_cast<size_t>(sw) * sh;
                snap.rgb.resize(pixels * 3);
                bool opaque = true;
                for (size_t p = 0; p < pixels; ++p) {
                    snap.rgb[3 * p] = canvas[4 * p]; snap.rgb[3 * p + 1] = canvas[4 * p + 1]; snap.rgb[3 * p + 2] = canvas[4 * p + 2];
                    if (canvas[4 * p + 3] != 255) opaque = false;
                }
                if (!opaque) {
                    snap.alpha.resize(pixels);
                    for (size_t p = 0; p < pixels; ++p) snap.alpha[p] = canvas[4 * p + 3];
                }
                // Browsers play a delay of 0 or 1/100 s as 1/10 s, so do the same.
                snap.delayMs = gcb.DelayTime <= 1 ? 100 : gcb.DelayTime * 10;
                out.frames.push_back(std::move(snap));

                // Dispose of the frame before the next one is drawn.
                if (gcb.DisposalMode == DISPOSE_BACKGROUND) {
                    for (int sr = 0; sr < d.Height; ++sr) {
                        const int y = d.Top + sr;
                        if (y < 0 || y >= sh) continue;
                        for (int sx = 0; sx < d.Width; ++sx) {
                            const int x = d.Left + sx;
                            if (x >= 0 && x < sw) std::memset(&canvas[(static_cast<size_t>(y) * sw + x) * 4], 0, 4);
                        }
                    }
                }
                else if (gcb.DisposalMode == DISPOSE_PREVIOUS) {
                    canvas = beforeFrame;
                }
            }
            ok = !bad;
        } while (false);

        int ce = 0;
        DGifCloseFile(gif, &ce);
        return ok;
    }

    wxImage GifFrameToImage(const GifDecoded& d, const GifDecoded::Frame& f) {
        wxImage image(d.width, d.height, false);
        if (!image.IsOk()) return image;
        std::memcpy(image.GetData(), f.rgb.data(), f.rgb.size());
        if (!f.alpha.empty()) {
            image.InitAlpha();
            std::memcpy(image.GetAlpha(), f.alpha.data(), f.alpha.size());
        }
        return image;
    }

} // namespace

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

// Loads a HEIF/HEIC/AVIF file. An image sequence (animated AVIF) becomes one frame per sample; anything else
// (or a libheif without sequence support) becomes a single frame holding the primary image.
bool Codec::LoadHEIFAnimation(const wxString& filepath, AnimatedImage& out) {
    out.Clear();

#ifdef GENZO_HEIF_SEQUENCES
    heif_context* ctx = heif_context_alloc();
    heif_error err = heif_context_read_from_file(ctx, filepath.utf8_str(), nullptr);
    if (err.code == heif_error_Ok && heif_context_has_sequence(ctx)) {
        heif_track* track = heif_context_get_track(ctx, 0); // 0 = the first visual track
        if (track) {
            const double msPerTick = 1000.0 / std::max<uint32_t>(1, heif_track_get_timescale(track));
            // libheif reports how often the file asks to be repeated; 0xFFFFFFFF is taken as "forever".
            const uint32_t repetitions = heif_track_get_number_of_repetitions(track);
            out.loopCount = repetitions == 0xFFFFFFFFu ? 0 : static_cast<int>(repetitions);
            heif_image* frame = nullptr;
            // The sequence ends when the decoder reports an error (heif_error_End_of_sequence). By default libheif
            // also replays the edit list, so an endlessly looping file would never end: cap the frame count.
            const size_t maxFrames = 10000;
            while (out.frames.size() < maxFrames
                && heif_track_decode_next_image(track, &frame, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr).code == heif_error_Ok) {
                int stride = 0;
                const uint8_t* data = heif_image_get_plane_readonly(frame, heif_channel_interleaved, &stride);
                const int w = heif_image_get_primary_width(frame), h = heif_image_get_primary_height(frame);
                const int delay = static_cast<int>(heif_image_get_duration(frame) * msPerTick + 0.5);
                if (data) out.frames.push_back({ ImageFromRGBA(w, h, data, static_cast<size_t>(stride)), delay > 0 ? delay : 100 });
                heif_image_release(frame);
                frame = nullptr;
            }
            heif_track_release(track);
        }
    }
    heif_context_free(ctx);
    if (out.IsOk()) return true;
    out.Clear();
#endif

    wxImage still = LoadHEIFImage(filepath);
    if (!still.IsOk()) return false;
    out = AnimatedImage::FromImage(still);
    return true;
}

// Attaches the raw Exif / XMP blobs to an image that has just been encoded into `ctx`.
// Failure here is not fatal: the picture is still good, it just won't carry that metadata.
static void AttachHeifMetadata(heif_context* ctx, const heif_image_handle* handle, const MetadataBlobs& md) {
    if (!md.exif.empty()) {
        // libheif finds the TIFF header itself and adds the 4-byte offset prefix HEIF wants.
        heif_error e = heif_context_add_exif_metadata(ctx, handle, md.exif.data(), static_cast<int>(md.exif.size()));
        if (e.code != heif_error_Ok) {
            wxLogWarning("libheif could not store the Exif block: %s", e.message);
        }
    }
    if (!md.xmp.empty()) {
        // heif_context_add_XMP_metadata2 needs libheif 1.12 or newer.
        heif_error e = heif_context_add_XMP_metadata2(ctx, handle, md.xmp.data(), static_cast<int>(md.xmp.size()),
            heif_metadata_compression_off);
        if (e.code != heif_error_Ok) {
            wxLogWarning("libheif could not store the XMP packet: %s", e.message);
        }
    }
}

// Copies a wxImage into a new libheif image (RGB, or RGBA if the wxImage has alpha). Caller releases it.
static heif_image* CreateHeifImage(const wxImage& img) {
    int width = img.GetWidth();
    int height = img.GetHeight();
    bool hasAlpha = img.HasAlpha();

    heif_image* h_image = nullptr;
    heif_chroma chroma = hasAlpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB;
    heif_error err = heif_image_create(width, height, heif_colorspace_RGB, chroma, &h_image);

    if (err.code != heif_error_Ok) {
        wxLogError("Failed to create HEIF image: %s", err.message);
        return nullptr;
    }

    err = heif_image_add_plane(h_image, heif_channel_interleaved, width, height, hasAlpha ? 32 : 24);
    if (err.code != heif_error_Ok) {
        wxLogError("Failed to add plane to HEIF image: %s", err.message);
        heif_image_release(h_image);
        return nullptr;
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

    return h_image;
}

// Adds every frame of `anim` to `ctx` as an AV1 image sequence (the animated half of an animated AVIF).
// Failure here is not fatal: the file still holds the still image, it just won't animate.
static bool AddHeifSequence(heif_context* ctx, const AnimatedImage& anim, int quality) {
#ifdef GENZO_HEIF_SEQUENCES
    const int width = anim.frames[0].image.GetWidth(), height = anim.frames[0].image.GetHeight();

    heif_encoder* encoder = nullptr;
    heif_error err = heif_context_get_encoder_for_format(ctx, heif_compression_AV1, &encoder);
    if (err.code != heif_error_Ok) {
        wxLogWarning("libheif could not get an AV1 encoder for the animation: %s", err.message);
        return false;
    }
    heif_encoder_set_lossy_quality(encoder, quality);

    heif_track_options* trackOptions = heif_track_options_alloc();
    heif_track_options_set_timescale(trackOptions, 1000); // frame durations below are in milliseconds
    heif_sequence_encoding_options* seqOptions = heif_sequence_encoding_options_alloc();
    heif_track* track = nullptr;
    err = heif_context_add_visual_sequence_track(ctx, static_cast<uint16_t>(width), static_cast<uint16_t>(height),
        heif_track_type_image_sequence, trackOptions, seqOptions, &track);
    heif_track_options_release(trackOptions);

    bool ok = err.code == heif_error_Ok && track;
    if (!ok) {
        wxLogWarning("libheif could not create the animation track: %s", err.message);
    }
    else {
        for (const AnimatedImage::Frame& frame : anim.frames) {
            heif_image* hImage = CreateHeifImage(FitToCanvas(frame.image, width, height));
            if (!hImage) { ok = false; break; }
            heif_image_set_duration(hImage, static_cast<uint32_t>(std::max(1, frame.delayMs)));
            err = heif_track_encode_sequence_image(track, hImage, encoder, seqOptions);
            heif_image_release(hImage);
            if (err.code != heif_error_Ok) {
                wxLogWarning("libheif could not encode an animation frame: %s", err.message);
                ok = false;
                break;
            }
        }
        if (ok) {
            err = heif_track_encode_end_of_sequence(track, encoder);
            ok = err.code == heif_error_Ok;
            if (!ok) wxLogWarning("libheif could not finish the animation: %s", err.message);
        }
        heif_track_release(track);
    }

    heif_sequence_encoding_options_release(seqOptions);
    heif_encoder_release(encoder);
    return ok;
#else
    wxLogWarning("Animated AVIF needs libheif 1.20 or newer. Only the first frame was saved.");
    return false;
#endif
}

bool Codec::SaveHEIFImage(const wxImage& img, const wxString& filePath, int quality, const MetadataBlobs* metadata) {
    return SaveHEIFImage(AnimatedImage::FromImage(img), filePath, quality, metadata);
}

bool Codec::SaveHEIFImage(const AnimatedImage& anim, const wxString& filePath, int quality, const MetadataBlobs* metadata) {
    if (!anim.IsOk()) {
        wxLogError("Invalid wxImage passed to HEIF encoder.");
        return false;
    }

    // The first frame is always written as the still (primary) image. It carries the Exif / XMP, and is what
    // viewers without image-sequence support show.
    const wxImage& img = anim.frames[0].image;

    heif_image* h_image = CreateHeifImage(img);
    if (!h_image) {
        return false;
    }

    heif_error err;
    heif_context* ctx = heif_context_alloc();
    heif_encoder* encoder = nullptr;

    wxFileName fileName(filePath);
    fileName.Normalize(wxPATH_NORM_ALL & wxPATH_NORM_ENV_VARS);
    const bool isAvif = fileName.GetExt() == "avif";
    if (isAvif) {
        err = heif_context_get_encoder_for_format(ctx, heif_compression_AV1, &encoder);
    }
    else {
        err = heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
    }

    bool success = false;

    if (err.code == heif_error_Ok) {
        heif_encoder_set_lossy_quality(encoder, quality);

        heif_image_handle* encodedHandle = nullptr;
        err = heif_context_encode_image(ctx, h_image, encoder, nullptr, metadata ? &encodedHandle : nullptr);
        if (err.code == heif_error_Ok) {
            if (metadata && encodedHandle) {
                AttachHeifMetadata(ctx, encodedHandle, *metadata);
            }
            if (isAvif && anim.IsAnimated()) {
                AddHeifSequence(ctx, anim, quality);
            }
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
        if (encodedHandle) {
            heif_image_handle_release(encodedHandle);
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

    // This line is disabled since it's the only one that requires C++17 instead of C++11.
    // quality = std::clamp(quality, 1, 100);
    quality = std::max(1, std::min(100, quality));

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

static void WriteBE32(wxFileOutputStream& out, uint32_t value) {
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

bool Codec::SaveICNS(const wxImage& img, const wxString& filePath) {
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


// Reads a whole file into `bytes`.
static bool ReadWholeFile(const wxString& filePath, std::vector<unsigned char>& bytes) {
    wxFileInputStream in(filePath);
    if (!in.IsOk()) return false;
    bytes.resize(static_cast<size_t>(in.GetLength()));
    return bytes.empty() || in.ReadAll(bytes.data(), bytes.size());
}

// Loads a WebP file, still or animated. libwebp's animation decoder hands back each frame already
// composited onto the full canvas, so a still image is just a one-frame animation.
bool Codec::LoadWebPAnimation(const wxString& filePath, AnimatedImage& out) {
    out.Clear();

    std::vector<unsigned char> bytes;
    if (!ReadWholeFile(filePath, bytes) || bytes.empty()) {
        wxLogError("Could not read '%s'.", filePath);
        return false;
    }

    WebPData data;
    WebPDataInit(&data);
    data.bytes = bytes.data();
    data.size = bytes.size();

    WebPAnimDecoderOptions options;
    if (!WebPAnimDecoderOptionsInit(&options)) return false;
    options.color_mode = MODE_RGBA;
    options.use_threads = 1;

    WebPAnimDecoder* decoder = WebPAnimDecoderNew(&data, &options);
    if (!decoder) {
        wxLogError("libwebp failed to decode '%s'.", filePath);
        return false;
    }

    WebPAnimInfo info;
    if (WebPAnimDecoderGetInfo(decoder, &info)) {
        out.loopCount = static_cast<int>(info.loop_count); // 0 = forever, same as AnimatedImage
        int previousEnd = 0;
        while (WebPAnimDecoderHasMoreFrames(decoder)) {
            uint8_t* rgba = nullptr;
            int endMs = 0; // WebPAnimDecoderGetNext reports when the frame stops being shown
            if (!WebPAnimDecoderGetNext(decoder, &rgba, &endMs)) break;
            const int delay = endMs - previousEnd;
            previousEnd = endMs;
            out.frames.push_back({ ImageFromRGBA(static_cast<int>(info.canvas_width), static_cast<int>(info.canvas_height), rgba,
                static_cast<size_t>(info.canvas_width) * 4), delay > 0 ? delay : 100 });
        }
    }
    WebPAnimDecoderDelete(decoder);

    if (!out.IsOk()) {
        out.Clear();
        wxLogError("libwebp failed to decode '%s'.", filePath);
        return false;
    }
    return true;
}

// Writes `anim` as a WebP file. libwebp's animation encoder works out the per-frame sub-rectangles and
// blending itself. quality is 1-100; 100 requests lossless encoding.
bool Codec::SaveWebPAnimation(const AnimatedImage& anim, const wxString& filePath, int quality) {
    if (!anim.IsOk()) {
        wxLogError("Invalid image passed to the WebP encoder.");
        return false;
    }
    quality = std::max(1, std::min(100, quality));
    const int width = anim.frames[0].image.GetWidth(), height = anim.frames[0].image.GetHeight();

    WebPAnimEncoderOptions options;
    WebPConfig config;
    if (!WebPAnimEncoderOptionsInit(&options) || !WebPConfigPreset(&config, WEBP_PRESET_DEFAULT, static_cast<float>(quality))) {
        wxLogError("libwebp version mismatch.");
        return false;
    }
    options.anim_params.loop_count = std::max(0, anim.loopCount);
    config.lossless = quality >= 100 ? 1 : 0;
    if (!WebPValidateConfig(&config)) {
        wxLogError("Invalid WebP encoder settings.");
        return false;
    }

    WebPAnimEncoder* encoder = WebPAnimEncoderNew(width, height, &options);
    if (!encoder) {
        wxLogError("Failed to create the WebP animation encoder.");
        return false;
    }

    bool ok = true;
    int timestampMs = 0;
    std::vector<unsigned char> rgba;
    for (const AnimatedImage::Frame& frame : anim.frames) {
        const wxImage image = FitToCanvas(frame.image, width, height);
        if (!image.IsOk()) { ok = false; break; }
        ImageToRGBA(image, rgba);

        WebPPicture picture;
        if (!WebPPictureInit(&picture)) { ok = false; break; }
        picture.use_argb = 1;
        picture.width = width;
        picture.height = height;
        ok = WebPPictureImportRGBA(&picture, rgba.data(), width * 4) && WebPAnimEncoderAdd(encoder, &picture, timestampMs, &config);
        if (!ok) wxLogError("Failed to encode a WebP frame: %s", WebPAnimEncoderGetError(encoder));
        WebPPictureFree(&picture);
        if (!ok) break;
        timestampMs += std::max(1, frame.delayMs);
    }

    WebPData output;
    WebPDataInit(&output);
    // A final empty frame tells the encoder where the last real frame ends.
    ok = ok && WebPAnimEncoderAdd(encoder, nullptr, timestampMs, nullptr) && WebPAnimEncoderAssemble(encoder, &output);
    if (!ok && output.bytes == nullptr) wxLogError("Failed to assemble the WebP file: %s", WebPAnimEncoderGetError(encoder));
    WebPAnimEncoderDelete(encoder);

    if (ok) {
        wxFileOutputStream out(filePath);
        ok = out.IsOk() && out.WriteAll(output.bytes, output.size);
        if (!ok) wxLogError("Could not write '%s'.", filePath);
    }
    WebPDataClear(&output);
    return ok;
}

#if wxUSE_STREAMS
namespace {

    int GifReadFromStream(GifFileType* gif, GifByteType* buf, int len) {
        wxInputStream* s = static_cast<wxInputStream*>(gif->UserData);
        s->Read(buf, static_cast<size_t>(len));
        return static_cast<int>(s->LastRead());
    }

    int GifWriteToStream(GifFileType* gif, const GifByteType* buf, int len) {
        wxOutputStream* s = static_cast<wxOutputStream*>(gif->UserData);
        s->Write(buf, static_cast<size_t>(len));
        return s->LastWrite() == static_cast<size_t>(len) ? len : 0;
    }

    // Encodes every frame of `anim` as a GIF into `stream` using giflib.
    bool GifSaveToStream(const AnimatedImage& anim, wxOutputStream& stream, bool verbose) {
        if (!anim.IsOk()) return false;

        wxString error;
        const bool ok = GifEncode(anim, GifWriteToStream, &stream, &error);
        if (!ok && verbose) wxLogError("Failed to save GIF: %s", error);
        return ok;
    }

    // Encodes `image` as a GIF into `stream` using giflib. Shared by the wxImage handler and Codec::SaveGIF.
    bool GifSaveToStream(const wxImage& image, wxOutputStream& stream, bool verbose) {
        return GifSaveToStream(AnimatedImage::FromImage(image), stream, verbose);
    }

    // Plugs giflib into wxImage. Once registered (see Codec::RegisterGIFHandler) it takes over from
    // wxWidgets' built-in GIF handler for wxImage::LoadFile / SaveFile / CanRead, with no wx rebuild needed.
    class GifLibHandler : public wxImageHandler {
    public:
        GifLibHandler() {
            SetName("GIF file (giflib)");
            SetExtension("gif");
            SetType(wxBITMAP_TYPE_GIF);
            SetMimeType("image/gif");
        }

        bool LoadFile(wxImage* image, wxInputStream& stream, bool verbose = true, int index = -1) override {
            const int frameIndex = index < 0 ? 0 : index;
            GifDecoded d;
            wxString error;
            if (!GifDecode(GifReadFromStream, &stream, frameIndex + 1, d, &error)) {
                if (verbose) wxLogError("Failed to load GIF: %s", error);
                return false;
            }
            *image = GifFrameToImage(d, d.frames[frameIndex]);
            return image->IsOk();
        }

        bool SaveFile(wxImage* image, wxOutputStream& stream, bool verbose = true) override {
            wxLogDebug("[GIF] GifLibHandler::SaveFile entered");
            const bool ok = image && GifSaveToStream(*image, stream, verbose);
            wxLogDebug("[GIF] GifLibHandler::SaveFile finished, ok=%d", ok ? 1 : 0);
            return ok;
        }

    protected:
        // wxImageHandler::GetImageCount() is a non-virtual wrapper around this.
        int DoGetImageCount(wxInputStream& stream) override {
            GifDecoded d;
            return GifDecode(GifReadFromStream, &stream, 1, d, nullptr) ? d.frameCount : 0;
        }

        bool DoCanRead(wxInputStream& stream) override {
            unsigned char sig[6] = {};
            if (!stream.ReadAll(sig, sizeof(sig))) return false;
            return std::memcmp(sig, "GIF87a", 6) == 0 || std::memcmp(sig, "GIF89a", 6) == 0;
        }
    };

} // namespace
#endif

void Codec::RegisterGIFHandler() {
#if wxUSE_STREAMS
    // Inserted at the front of wxImage's handler list. NOTE: with wxWidgets 3.3.3 this did not
    // reliably take over wxImage::SaveFile(..., wxBITMAP_TYPE_GIF), so Genzo calls Codec::SaveGIF /
    // Codec::LoadGIF directly instead. The registration is kept for any other wxImage GIF use.
    wxImage::InsertHandler(new GifLibHandler);
    const wxImageHandler* active = wxImage::FindHandler(wxBITMAP_TYPE_GIF);
    wxLogDebug("[GIF] giflib handler registered; wxImage's GIF handler is now: %s",
        active ? active->GetName() : wxString("(none)"));
#endif
}

bool Codec::SaveGIF(const AnimatedImage& anim, const wxString& filePath) {
#if wxUSE_STREAMS
    if (!anim.IsOk()) return false;

    wxFileOutputStream file(filePath);
    if (!file.IsOk()) {
        wxLogError("Could not open '%s' for writing.", filePath);
        return false;
    }
    wxBufferedOutputStream out(file);

    if (!GifSaveToStream(anim, out, true)) return false;

    if (!out.Close()) {
        wxLogError("Failed to finish writing '%s'.", filePath);
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool Codec::SaveGIF(const wxImage& image, const wxString& filePath) {
    return SaveGIF(AnimatedImage::FromImage(image), filePath);
}

bool Codec::LoadGIFAnimation(const wxString& filePath, AnimatedImage& out) {
    out.Clear();
#if wxUSE_STREAMS
    wxFileInputStream file(filePath);
    if (!file.IsOk()) {
        wxLogError("Could not open '%s' for reading.", filePath);
        return false;
    }
    wxBufferedInputStream in(file);

    GifDecoded d;
    wxString error;
    if (!GifDecode(GifReadFromStream, &in, -1, d, &error)) {
        wxLogError("Failed to load GIF: %s", error);
        return false;
    }

    out.loopCount = d.loopCount;
    for (const GifDecoded::Frame& f : d.frames) {
        out.frames.push_back({ GifFrameToImage(d, f), f.delayMs });
        if (!out.frames.back().image.IsOk()) { out.Clear(); return false; }
    }
    return out.IsOk();
#else
    return false;
#endif
}

wxImage Codec::LoadGIF(const wxString& filePath) {
    AnimatedImage anim;
    return LoadGIFAnimation(filePath, anim) ? anim.frames[0].image : wxImage();
}