#pragma once
#include <wx/string.h>
#include <vector>

// Raw metadata blobs lifted out of one image container so they can be dropped
// into another. Nothing here is parsed into individual tags or re-serialised:
// the bytes that come out are the bytes that went in.
struct MetadataBlobs {
	using Bytes = std::vector<unsigned char>;

	enum Kind : unsigned { Exif = 1, Xmp = 2, Iptc = 4 };

	// Bare TIFF-structured Exif block, starting at the "II*\0" / "MM\0*" header.
	// Container wrappers ("Exif\0\0" in JPEG APP1, the 4-byte offset prefix in
	// HEIF, ...) are stripped on read and re-added on write.
	Bytes exif;

	// XMP packet (UTF-8 XML).
	Bytes xmp;

	// IPTC-IIM datasets (the payload of Photoshop image resource 0x0404).
	Bytes iptc;

	bool IsEmpty() const { return exif.empty() && xmp.empty() && iptc.empty(); }

	// Bitmask of Kind values that are actually present.
	unsigned Kinds() const {
		return (exif.empty() ? 0u : static_cast<unsigned>(Exif))
			| (xmp.empty() ? 0u : static_cast<unsigned>(Xmp))
			| (iptc.empty() ? 0u : static_cast<unsigned>(Iptc));
	}

	// e.g. Exif | Xmp  ->  "Exif, XMP"
	static wxString KindsToString(unsigned kinds);

	void Clear() { exif.clear(); xmp.clear(); iptc.clear(); }
};

namespace Metadata {
	// Reads Exif / XMP / IPTC out of an image file into `out`.
	// The container is identified from the file's magic bytes, not its extension.
	// `out` is cleared first; if the container is unsupported or carries nothing
	// it is left empty.
	//
	// Readable: JPEG, PNG, WebP, HEIF/HEIC/AVIF, JPEG 2000 (JP2/JPF/JPX boxed).
	void ReadFromFile(const wxString& filePath, MetadataBlobs& out);

	// Which kinds of metadata an output format can carry, as a bitmask of
	// MetadataBlobs::Kind. 0 means "cannot carry any". `extension` is the
	// output file extension without the dot, any case.
	//
	//   jpg/jpeg/jpe/jfif   Exif + XMP + IPTC
	//   jp2/jpf/jpx         Exif + XMP + IPTC  (raw .j2k/.j2c codestreams: none)
	//   png/webp            Exif + XMP
	//   heic/heif/avif      Exif + XMP
	unsigned WritableKinds(const wxString& extension);

	// Splices `metadata` into an image file that has ALREADY been written,
	// by rewriting its container (JPEG segments, PNG chunks, RIFF/WebP chunks,
	// JP2 boxes). Pixel data is never touched. The file is replaced atomically
	// (temp file + rename). Kinds the container can't hold are skipped.
	//
	// HEIF/AVIF is NOT handled here: libheif has to attach metadata before the
	// file is written, so Codec::SaveHEIFImage does it itself.
	bool EmbedInFile(const wxString& filePath, const MetadataBlobs& metadata);

	// Sets the Orientation tag (0x0112) inside a bare TIFF/Exif block to 1
	// ("upright"). Use this when the pixels have already been rotated to match
	// the tag, otherwise viewers rotate the image a second time.
	void ResetExifOrientation(MetadataBlobs::Bytes& exif);
}