#include "Metadata.h"
#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/log.h>
#include <libheif/heif.h>
#include <webp/mux.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

// How each container stores the three kinds of metadata:
//
//            Exif                          XMP                              IPTC
//   JPEG     APP1 "Exif\0\0"+TIFF          APP1 "http://ns.adobe.com/xap/"  APP13 Photoshop 8BIM 0x0404
//   PNG      eXIf chunk                    iTXt "XML:com.adobe.xmp"         (no standard location)
//   WebP     EXIF chunk                    XMP  chunk                       (no standard location)
//   HEIF     'Exif' item (4-byte offset)   'mime' item application/rdf+xml (no standard location)
//   JP2      uuid box "JpgTiffExif->JP2"   uuid box BE7ACFCB-...            uuid box 33C7A4D2-...
//
// Reading and writing is done on the container, so the blobs are carried over
// verbatim and are never decoded or re-encoded.

namespace {

    using Bytes = MetadataBlobs::Bytes;

    // ----------------------------------------------------------------------------
    // Small helpers
    // ----------------------------------------------------------------------------

    inline uint32_t BE16(const unsigned char* p) { return (uint32_t(p[0]) << 8) | p[1]; }

    inline uint32_t BE32(const unsigned char* p) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    }

    inline void PutBE16(Bytes& b, uint32_t v) {
        b.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
        b.push_back(static_cast<unsigned char>(v & 0xFF));
    }

    inline void PutBE32(Bytes& b, uint32_t v) {
        b.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
        b.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
        b.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
        b.push_back(static_cast<unsigned char>(v & 0xFF));
    }

    inline void Append(Bytes& dst, const void* src, size_t n) {
        const unsigned char* p = static_cast<const unsigned char*>(src);
        dst.insert(dst.end(), p, p + n);
    }

    inline void Append(Bytes& dst, const Bytes& src) {
        dst.insert(dst.end(), src.begin(), src.end());
    }

    inline bool Match(const unsigned char* p, size_t avail, const void* literal, size_t n) {
        return avail >= n && std::memcmp(p, literal, n) == 0;
    }

    constexpr uint32_t Fourcc(char a, char b, char c, char d) {
        return (uint32_t(static_cast<unsigned char>(a)) << 24) | (uint32_t(static_cast<unsigned char>(b)) << 16)
            | (uint32_t(static_cast<unsigned char>(c)) << 8) | uint32_t(static_cast<unsigned char>(d));
    }

    bool ReadWholeFile(const wxString& path, Bytes& out) {
        wxFile f(path, wxFile::read);
        if (!f.IsOpened()) return false;

        const wxFileOffset len = f.Length();
        if (len <= 0) return false;

        out.resize(static_cast<size_t>(len));
        const auto got = f.Read(out.data(), out.size());
        return got >= 0 && static_cast<size_t>(got) == out.size();
    }

    // Writes next to the destination and renames over it, so a failure half way
    // through can never leave a truncated image behind.
    bool WriteWholeFile(const wxString& path, const Bytes& data) {
        const wxString tmp = path + ".genzo-tmp"; {
            wxFile f(tmp, wxFile::write);
            if (!f.IsOpened() || f.Write(data.data(), data.size()) != data.size()) {
                f.Close();
                wxRemoveFile(tmp);
                return false;
            }
        }
        if (!wxRenameFile(tmp, path, true)) {
            wxRemoveFile(tmp);
            return false;
        }
        return true;
    }

    // ----------------------------------------------------------------------------
    // Exif helpers
    // ----------------------------------------------------------------------------

    bool LooksLikeTiff(const unsigned char* p, size_t n) {
        return n >= 8 &&
            ((p[0] == 'I' && p[1] == 'I' && p[2] == 42 && p[3] == 0) ||
                (p[0] == 'M' && p[1] == 'M' && p[2] == 0 && p[3] == 42));
    }

    Bytes ExtractTiff(const unsigned char* p, size_t n) {
        const size_t window = std::min<size_t>(n, 64);
        for (size_t i = 0; i + 8 <= n && i < window; ++i) {
            if (LooksLikeTiff(p + i, n - i)) return Bytes(p + i, p + n);
        }
        return Bytes();
    }

    bool LooksLikeXmp(const unsigned char* p, size_t n) {
        static const char a[] = "<x:xmpmeta";
        static const char b[] = "<?xpacket";
        return std::search(p, p + n, a, a + sizeof(a) - 1) != p + n ||
            std::search(p, p + n, b, b + sizeof(b) - 1) != p + n;
    }

    // ----------------------------------------------------------------------------
    // Container detection
    // ----------------------------------------------------------------------------

    enum class Container { Unknown, Jpeg, Png, WebP, Jp2, Heif };

    const unsigned char kPngSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    const unsigned char kJp2Sig[12] = { 0x00, 0x00, 0x00, 0x0C, 'j', 'P', ' ', ' ', 0x0D, 0x0A, 0x87, 0x0A };

    Container Sniff(const unsigned char* d, size_t n) {
        if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return Container::Jpeg;
        if (Match(d, n, kPngSig, sizeof(kPngSig))) return Container::Png;
        if (n >= 12 && !std::memcmp(d, "RIFF", 4) && !std::memcmp(d + 8, "WEBP", 4)) return Container::WebP;
        if (Match(d, n, kJp2Sig, sizeof(kJp2Sig))) return Container::Jp2;
        if (n >= 12 && !std::memcmp(d + 4, "ftyp", 4)) return Container::Heif;
        return Container::Unknown;
    }

    // ============================================================================
    // JPEG
    // ============================================================================

    const unsigned char kExifApp1Id[6] = { 'E', 'x', 'i', 'f', 0, 0 };
    const char kXmpApp1Id[] = "http://ns.adobe.com/xap/1.0/";
    const char kPsApp13Id[] = "Photoshop 3.0";

    struct JpegSeg {
        size_t start = 0;
        size_t total = 0;
        unsigned char marker = 0;
        const unsigned char* payload = nullptr;
        size_t plen = 0;
    };

    bool NextJpegSegment(const Bytes& f, size_t& pos, JpegSeg& s) {
        while (pos + 1 < f.size() && f[pos] == 0xFF && f[pos + 1] == 0xFF) ++pos;

        if (pos + 4 > f.size() || f[pos] != 0xFF) return false;

        const unsigned char m = f[pos + 1];
        if (m == 0x00 || m == 0x01 || (m >= 0xD0 && m <= 0xDA)) return false;

        const size_t len = BE16(f.data() + pos + 2);
        if (len < 2 || pos + 2 + len > f.size()) return false;

        s.start = pos;
        s.total = 2 + len;
        s.marker = m;
        s.payload = f.data() + pos + 4;
        s.plen = len - 2;
        pos += s.total;
        return true;
    }

    bool IsExifSeg(const JpegSeg& s) { return s.marker == 0xE1 && Match(s.payload, s.plen, kExifApp1Id, sizeof(kExifApp1Id)); }
    bool IsXmpSeg(const JpegSeg& s) { return s.marker == 0xE1 && Match(s.payload, s.plen, kXmpApp1Id, sizeof(kXmpApp1Id)); }
    bool IsPsSeg(const JpegSeg& s) { return s.marker == 0xED && Match(s.payload, s.plen, kPsApp13Id, sizeof(kPsApp13Id)); }

    void FindIptcInPhotoshopBlock(const unsigned char* p, size_t n, Bytes& iptc) {
        size_t i = sizeof(kPsApp13Id);
        while (i + 12 <= n) {
            if (std::memcmp(p + i, "8BIM", 4) != 0) break;

            const uint32_t id = BE16(p + i + 4);
            size_t nameBytes = 1 + size_t(p[i + 6]);
            if (nameBytes & 1) ++nameBytes;

            const size_t sizePos = i + 6 + nameBytes;
            if (sizePos + 4 > n) break;

            const size_t dataLen = BE32(p + sizePos);
            const size_t dataPos = sizePos + 4;
            if (dataLen > n - dataPos) break;

            if (id == 0x0404 && iptc.empty()) iptc.assign(p + dataPos, p + dataPos + dataLen);

            i = dataPos + dataLen + (dataLen & 1);
        }
    }

    void ReadJpegInto(const Bytes& f, MetadataBlobs& out) {
        size_t pos = 2; // past SOI
        JpegSeg s;
        while (NextJpegSegment(f, pos, s)) {
            if (IsExifSeg(s)) {
                if (out.exif.empty()) out.exif = ExtractTiff(s.payload + 6, s.plen - 6);
            }
            else if (IsXmpSeg(s)) {
                if (out.xmp.empty()) out.xmp.assign(s.payload + sizeof(kXmpApp1Id), s.payload + s.plen);
            }
            else if (IsPsSeg(s)) {
                FindIptcInPhotoshopBlock(s.payload, s.plen, out.iptc);
            }
        }
    }

    void EmitJpegSegment(Bytes& out, unsigned char marker, const Bytes& payload, const char* what) {
        if (payload.size() + 2 > 0xFFFF) {
            wxLogWarning("%s is too large for a JPEG marker segment and was not copied.", what);
            return;
        }
        out.push_back(0xFF);
        out.push_back(marker);
        PutBE16(out, static_cast<uint32_t>(payload.size() + 2));
        Append(out, payload);
    }

    Bytes BuildJpegSegments(const MetadataBlobs& md) {
        Bytes out;

        if (!md.exif.empty()) {
            Bytes body(kExifApp1Id, kExifApp1Id + sizeof(kExifApp1Id));
            Append(body, md.exif);
            EmitJpegSegment(out, 0xE1, body, "The Exif block");
        }
        if (!md.xmp.empty()) {
            Bytes body;
            Append(body, kXmpApp1Id, sizeof(kXmpApp1Id));
            Append(body, md.xmp);
            EmitJpegSegment(out, 0xE1, body, "The XMP packet");
        }
        if (!md.iptc.empty()) {
            Bytes body;
            Append(body, kPsApp13Id, sizeof(kPsApp13Id));
            Append(body, "8BIM", 4);
            PutBE16(body, 0x0404);
            body.push_back(0);
            body.push_back(0);
            PutBE32(body, static_cast<uint32_t>(md.iptc.size()));
            Append(body, md.iptc);
            if (md.iptc.size() & 1) body.push_back(0);
            EmitJpegSegment(out, 0xED, body, "The IPTC block");
        }
        return out;
    }

    bool EmbedJpeg(Bytes& f, const MetadataBlobs& md) {
        if (f.size() < 4 || f[0] != 0xFF || f[1] != 0xD8) return false;

        const Bytes add = BuildJpegSegments(md);

        Bytes out(f.begin(), f.begin() + 2);
        out.reserve(f.size() + add.size());

        size_t pos = 2;
        bool inserted = false;
        JpegSeg s;
        while (NextJpegSegment(f, pos, s)) {
            if (!inserted && s.marker != 0xE0) {
                Append(out, add);
                inserted = true;
            }
            const bool drop = (!md.exif.empty() && IsExifSeg(s))
                || (!md.xmp.empty() && IsXmpSeg(s))
                || (!md.iptc.empty() && IsPsSeg(s));
            if (!drop) out.insert(out.end(), f.begin() + s.start, f.begin() + s.start + s.total);
        }
        if (!inserted) Append(out, add);

        if (pos + 2 > f.size() || f[pos] != 0xFF || f[pos + 1] != 0xDA) return false;

        out.insert(out.end(), f.begin() + pos, f.end());
        f.swap(out);
        return true;
    }

    // ============================================================================
    // PNG
    // ============================================================================

    const char kPngXmpKeyword[] = "XML:com.adobe.xmp";

    uint32_t PngCrc32(const unsigned char* p, size_t n) {
        static uint32_t table[256];
        static bool ready = false;
        if (!ready) {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                table[i] = c;
            }
            ready = true;
        }
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    }

    bool IsPngXmpChunk(const unsigned char* d, size_t n) {
        return n >= sizeof(kPngXmpKeyword) + 4 && std::memcmp(d, kPngXmpKeyword, sizeof(kPngXmpKeyword)) == 0;
    }

    void ReadPngXmp(const unsigned char* d, size_t n, Bytes& xmp) {
        if (!xmp.empty() || !IsPngXmpChunk(d, n)) return;

        size_t i = sizeof(kPngXmpKeyword);
        const bool compressed = d[i] != 0;
        i += 2;
        while (i < n && d[i]) ++i;
        ++i;
        while (i < n && d[i]) ++i;
        ++i;
        if (i > n) return;

        if (compressed) {
            wxLogWarning("This PNG stores its XMP compressed, which isn't supported yet; XMP was not copied.");
            return;
        }
        xmp.assign(d + i, d + n);
    }

    void ReadPngInto(const Bytes& f, MetadataBlobs& out) {
        size_t pos = sizeof(kPngSig);
        while (pos + 12 <= f.size()) {
            const size_t len = BE32(f.data() + pos);
            if (len > f.size() - pos - 12) break;

            const unsigned char* type = f.data() + pos + 4;
            const unsigned char* data = f.data() + pos + 8;

            if (!std::memcmp(type, "eXIf", 4)) {
                if (out.exif.empty()) out.exif = ExtractTiff(data, len);
            }
            else if (!std::memcmp(type, "iTXt", 4)) {
                ReadPngXmp(data, len, out.xmp);
            }
            else if (!std::memcmp(type, "IEND", 4)) {
                break;
            }
            pos += 12 + len;
        }
    }

    void AppendPngChunk(Bytes& out, const char type[4], const Bytes& data) {
        PutBE32(out, static_cast<uint32_t>(data.size()));
        const size_t crcStart = out.size();
        Append(out, type, 4);
        Append(out, data);
        PutBE32(out, PngCrc32(out.data() + crcStart, 4 + data.size()));
    }

    bool EmbedPng(Bytes& f, const MetadataBlobs& md) {
        if (f.size() < sizeof(kPngSig) + 12 || std::memcmp(f.data(), kPngSig, sizeof(kPngSig)) != 0) return false;

        size_t pos = sizeof(kPngSig);
        if (std::memcmp(f.data() + pos + 4, "IHDR", 4) != 0) return false;
        const size_t ihdrEnd = pos + 12 + BE32(f.data() + pos);
        if (ihdrEnd > f.size()) return false;

        Bytes out(f.begin(), f.begin() + ihdrEnd);
        out.reserve(f.size() + md.exif.size() + md.xmp.size() + 64);

        if (!md.exif.empty()) AppendPngChunk(out, "eXIf", md.exif);
        if (!md.xmp.empty()) {
            Bytes body;
            Append(body, kPngXmpKeyword, sizeof(kPngXmpKeyword));
            body.push_back(0);
            body.push_back(0);
            body.push_back(0);
            body.push_back(0);
            Append(body, md.xmp);
            AppendPngChunk(out, "iTXt", body);
        }

        pos = ihdrEnd;
        while (pos + 12 <= f.size()) {
            const size_t len = BE32(f.data() + pos);
            if (len > f.size() - pos - 12) return false;
            const size_t total = 12 + len;

            const unsigned char* type = f.data() + pos + 4;
            const unsigned char* data = f.data() + pos + 8;
            const bool drop = (!md.exif.empty() && !std::memcmp(type, "eXIf", 4))
                || (!md.xmp.empty() && !std::memcmp(type, "iTXt", 4) && IsPngXmpChunk(data, len));
            if (!drop) out.insert(out.end(), f.begin() + pos, f.begin() + pos + total);
            pos += total;
        }
        f.swap(out);
        return true;
    }

    // ============================================================================
    // WebP
    // ============================================================================

    void ReadWebPInto(const Bytes& f, MetadataBlobs& out) {
        WebPData in;
        WebPDataInit(&in);
        in.bytes = f.data();
        in.size = f.size();

        WebPMux* mux = WebPMuxCreate(&in, 0);
        if (!mux) return;

        WebPData chunk;
        WebPDataInit(&chunk);
        if (WebPMuxGetChunk(mux, "EXIF", &chunk) == WEBP_MUX_OK && chunk.size > 0) {
            if (out.exif.empty()) out.exif = ExtractTiff(chunk.bytes, chunk.size);
        }
        WebPDataInit(&chunk);
        if (WebPMuxGetChunk(mux, "XMP ", &chunk) == WEBP_MUX_OK && chunk.size > 0) {
            if (out.xmp.empty()) out.xmp.assign(chunk.bytes, chunk.bytes + chunk.size);
        }

        WebPMuxDelete(mux);
    }

    bool SetWebPChunk(WebPMux* mux, const char fourcc[4], const Bytes& payload) {
        WebPData d;
        WebPDataInit(&d);
        d.bytes = payload.data();
        d.size = payload.size();
        return WebPMuxSetChunk(mux, fourcc, &d, 1) == WEBP_MUX_OK;
    }

    bool EmbedWebP(Bytes& f, const MetadataBlobs& md) {
        WebPData in;
        WebPDataInit(&in);
        in.bytes = f.data();
        in.size = f.size();

        WebPMux* mux = WebPMuxCreate(&in, 0);
        if (!mux) return false;

        bool ok = true;
        if (ok && !md.exif.empty()) ok = SetWebPChunk(mux, "EXIF", md.exif);
        if (ok && !md.xmp.empty()) ok = SetWebPChunk(mux, "XMP ", md.xmp);

        WebPData out;
        WebPDataInit(&out);
        if (ok) ok = (WebPMuxAssemble(mux, &out) == WEBP_MUX_OK);

        WebPMuxDelete(mux);
        if (ok) f.assign(out.bytes, out.bytes + out.size);
        WebPDataClear(&out);
        return ok;
    }

    // ============================================================================
    // JPEG 2000
    // ============================================================================

    const unsigned char kUuidExif[16] = { 'J', 'p', 'g', 'T', 'i', 'f', 'f', 'E', 'x', 'i', 'f', '-', '>', 'J', 'P', '2' };
    const unsigned char kUuidXmp[16] = { 0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8, 0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC };
    const unsigned char kUuidIptc[16] = { 0x33, 0xC7, 0xA4, 0xD2, 0xB8, 0x1D, 0x47, 0x23, 0xA0, 0xBA, 0xF1, 0xA3, 0xE0, 0x97, 0xAD, 0x38 };

    struct Jp2Box {
        size_t start = 0;
        size_t hdr = 8;
        size_t total = 0;
        uint32_t type = 0;
        bool toEof = false;
    };

    bool NextJp2Box(const Bytes& f, size_t pos, Jp2Box& b) {
        if (pos + 8 > f.size()) return false;

        uint64_t len = BE32(f.data() + pos);
        b.type = BE32(f.data() + pos + 4);
        b.hdr = 8;
        b.toEof = false;

        if (len == 1) {
            if (pos + 16 > f.size()) return false;
            len = (uint64_t(BE32(f.data() + pos + 8)) << 32) | BE32(f.data() + pos + 12);
            b.hdr = 16;
        }
        else if (len == 0) {
            len = f.size() - pos;
            b.toEof = true;
        }
        if (len < b.hdr || len > f.size() - pos) return false;

        b.start = pos;
        b.total = static_cast<size_t>(len);
        return true;
    }

    void ReadJp2Into(const Bytes& f, MetadataBlobs& out) {
        size_t pos = 0;
        Jp2Box b;
        while (NextJp2Box(f, pos, b)) {
            const unsigned char* p = f.data() + b.start + b.hdr;
            const size_t n = b.total - b.hdr;

            if (b.type == Fourcc('u', 'u', 'i', 'd') && n > 16) {
                if (!std::memcmp(p, kUuidExif, 16)) {
                    if (out.exif.empty()) out.exif = ExtractTiff(p + 16, n - 16);
                }
                else if (!std::memcmp(p, kUuidXmp, 16)) {
                    if (out.xmp.empty()) out.xmp.assign(p + 16, p + n);
                }
                else if (!std::memcmp(p, kUuidIptc, 16)) {
                    if (out.iptc.empty()) out.iptc.assign(p + 16, p + n);
                }
            }
            else if (b.type == Fourcc('x', 'm', 'l', ' ') && out.xmp.empty() && LooksLikeXmp(p, n)) {
                out.xmp.assign(p, p + n);
            }
            pos += b.total;
        }
    }

    void AppendUuidBox(Bytes& out, const unsigned char uuid[16], const Bytes& payload) {
        PutBE32(out, static_cast<uint32_t>(8 + 16 + payload.size()));
        Append(out, "uuid", 4);
        Append(out, uuid, 16);
        Append(out, payload);
    }

    bool EmbedJp2(Bytes& f, const MetadataBlobs& md) {
        Bytes out;
        out.reserve(f.size() + md.exif.size() + md.xmp.size() + md.iptc.size() + 96);

        size_t pos = 0;
        bool sawCodestream = false;
        Jp2Box b;
        while (pos < f.size()) {
            if (!NextJp2Box(f, pos, b)) return false;

            const unsigned char* p = f.data() + b.start + b.hdr;
            const size_t n = b.total - b.hdr;

            if (b.type == Fourcc('j', 'p', '2', 'c')) sawCodestream = true;

            bool drop = false;
            if (b.type == Fourcc('u', 'u', 'i', 'd') && n >= 16) {
                drop = (!md.exif.empty() && !std::memcmp(p, kUuidExif, 16))
                    || (!md.xmp.empty() && !std::memcmp(p, kUuidXmp, 16))
                    || (!md.iptc.empty() && !std::memcmp(p, kUuidIptc, 16));
            }
            if (!drop) {
                const size_t at = out.size();
                out.insert(out.end(), f.begin() + b.start, f.begin() + b.start + b.total);
                if (b.toEof) {
                    if (b.total > 0xFFFFFFFFull) return false;
                    for (int i = 0; i < 4; ++i) out[at + i] = static_cast<unsigned char>((b.total >> (24 - 8 * i)) & 0xFF);
                }
            }
            pos += b.total;
        }
        if (!sawCodestream) return false;

        if (!md.exif.empty()) AppendUuidBox(out, kUuidExif, md.exif);
        if (!md.xmp.empty()) AppendUuidBox(out, kUuidXmp, md.xmp);
        if (!md.iptc.empty()) AppendUuidBox(out, kUuidIptc, md.iptc);

        f.swap(out);
        return true;
    }

    // ============================================================================
    // HEIF / HEIC / AVIF  (read side)
    // ============================================================================

    void ReadHeifInto(const wxString& path, MetadataBlobs& out) {
        heif_context* ctx = heif_context_alloc();
        if (!ctx) return;

        heif_error err = heif_context_read_from_file(ctx, path.utf8_str(), nullptr);
        heif_image_handle* handle = nullptr;
        if (err.code == heif_error_Ok) err = heif_context_get_primary_image_handle(ctx, &handle);

        if (err.code == heif_error_Ok && handle) {
            int count = heif_image_handle_get_number_of_metadata_blocks(handle, nullptr);
            std::vector<heif_item_id> ids(count > 0 ? count : 0);
            if (count > 0) count = heif_image_handle_get_list_of_metadata_block_IDs(handle, nullptr, ids.data(), count);

            for (int i = 0; i < count; ++i) {
                const char* type = heif_image_handle_get_metadata_type(handle, ids[i]);
                const char* contentType = heif_image_handle_get_metadata_content_type(handle, ids[i]);
                const size_t size = heif_image_handle_get_metadata_size(handle, ids[i]);
                if (!type || size == 0) continue;

                const bool isExif = std::strcmp(type, "Exif") == 0;
                const bool isXmp = std::strcmp(type, "mime") == 0 && contentType
                    && std::strcmp(contentType, "application/rdf+xml") == 0;
                if (!isExif && !isXmp) continue;

                Bytes buf(size);
                if (heif_image_handle_get_metadata(handle, ids[i], buf.data()).code != heif_error_Ok) continue;

                if (isExif && out.exif.empty()) {
                    out.exif = ExtractTiff(buf.data(), buf.size());
                    Metadata::ResetExifOrientation(out.exif);
                }
                else if (isXmp && out.xmp.empty()) {
                    out.xmp = std::move(buf);
                }
            }
        }

        if (handle) heif_image_handle_release(handle);
        heif_context_free(ctx);
    }

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

wxString MetadataBlobs::KindsToString(unsigned kinds) {
    wxString s;
    auto add = [&s](const char* name) {
        if (!s.empty()) s += ", ";
        s += name;
        };
    if (kinds & Exif) add("EXIF");
    if (kinds & Xmp) add("XMP");
    if (kinds & Iptc) add("IPTC");
    return s;
}

namespace Metadata {

    void ReadFromFile(const wxString& filePath, MetadataBlobs& out) {
        out.Clear();

        unsigned char head[12] = { 0 };
        size_t headLen = 0; {
            wxFile f(filePath, wxFile::read);
            if (!f.IsOpened()) return;
            const auto got = f.Read(head, sizeof(head));
            headLen = got > 0 ? static_cast<size_t>(got) : 0;
        }

        const Container container = Sniff(head, headLen);
        if (container == Container::Unknown) return;
        if (container == Container::Heif) { ReadHeifInto(filePath, out); return; }

        Bytes data;
        if (!ReadWholeFile(filePath, data)) return;

        switch (container) {
        case Container::Jpeg: ReadJpegInto(data, out); break;
        case Container::Png:  ReadPngInto(data, out);  break;
        case Container::WebP: ReadWebPInto(data, out); break;
        case Container::Jp2:  ReadJp2Into(data, out);  break;
        default:              break;
        }
    }

    unsigned WritableKinds(const wxString& extension) {
        const wxString ext = extension.Lower();
        const unsigned all = MetadataBlobs::Exif | MetadataBlobs::Xmp | MetadataBlobs::Iptc;
        const unsigned noIptc = MetadataBlobs::Exif | MetadataBlobs::Xmp;

        if (ext == "jpg" || ext == "jpeg" || ext == "jpe" || ext == "jfif") return all;
        if (ext == "jp2" || ext == "jpf" || ext == "jpx") return all;
        if (ext == "png" || ext == "webp") return noIptc;
        if (ext == "heic" || ext == "heif" || ext == "avif") return noIptc;
        return 0;
    }

    bool EmbedInFile(const wxString& filePath, const MetadataBlobs& metadata) {
        if (metadata.IsEmpty()) return true;

        Bytes data;
        if (!ReadWholeFile(filePath, data)) {
            wxLogWarning("Could not reopen '%s' to add metadata.", filePath);
            return false;
        }

        bool ok = false;
        switch (Sniff(data.data(), data.size())) {
        case Container::Jpeg: ok = EmbedJpeg(data, metadata); break;
        case Container::Png:  ok = EmbedPng(data, metadata); break;
        case Container::WebP: ok = EmbedWebP(data, metadata); break;
        case Container::Jp2:  ok = EmbedJp2(data, metadata); break;
        default:
            wxLogWarning("'%s' is in a container that can't carry metadata.", filePath);
            return false;
        }

        if (!ok) {
            wxLogWarning("Failed to add metadata to '%s'. The image was saved without it.", filePath);
            return false;
        }
        if (!WriteWholeFile(filePath, data)) {
            wxLogWarning("Failed to write metadata into '%s'. The image was saved without it.", filePath);
            return false;
        }
        return true;
    }

    void ResetExifOrientation(MetadataBlobs::Bytes& exif) {
        if (!LooksLikeTiff(exif.data(), exif.size())) return;

        const bool little = exif[0] == 'I';
        auto rd16 = [&](size_t o) -> uint32_t {
            return little ? (uint32_t(exif[o]) | (uint32_t(exif[o + 1]) << 8))
                : ((uint32_t(exif[o]) << 8) | uint32_t(exif[o + 1]));
            };
        auto rd32 = [&](size_t o) -> uint32_t {
            return little ? (uint32_t(exif[o]) | (uint32_t(exif[o + 1]) << 8) | (uint32_t(exif[o + 2]) << 16) | (uint32_t(exif[o + 3]) << 24))
                : ((uint32_t(exif[o]) << 24) | (uint32_t(exif[o + 1]) << 16) | (uint32_t(exif[o + 2]) << 8) | uint32_t(exif[o + 3]));
            };

        const size_t ifd0 = rd32(4);
        if (ifd0 + 2 > exif.size()) return;

        const size_t entries = rd16(ifd0);
        for (size_t i = 0; i < entries; ++i) {
            const size_t e = ifd0 + 2 + i * 12;
            if (e + 12 > exif.size()) return;

            if (rd16(e) == 0x0112 && rd16(e + 2) == 3 && rd32(e + 4) == 1) {
                exif[e + 8] = little ? 1 : 0;
                exif[e + 9] = little ? 0 : 1;
                return;
            }
        }
    }

}