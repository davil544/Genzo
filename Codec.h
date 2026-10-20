#pragma once
#include <wx/image.h>
#include <wx/string.h>
#include <vector>
#include "Metadata.h"

// A decoded animation (GIF, WebP, AVIF). Every frame is a full canvas that is already composited,
// so frames can be shown or re-encoded one after another without caring about disposal or blending.
// A still image is simply an AnimatedImage with one frame.
struct AnimatedImage {
	struct Frame {
		wxImage image;
		int delayMs = 100; // how long this frame stays on screen
	};

	std::vector<Frame> frames;
	int loopCount = 0; // how many times the animation plays; 0 = forever

	bool IsOk() const { return !frames.empty() && frames.front().image.IsOk(); }
	bool IsAnimated() const { return frames.size() > 1; }
	void Clear() { frames.clear(); loopCount = 0; }

	static AnimatedImage FromImage(const wxImage& image) {
		AnimatedImage result;
		result.frames.push_back({ image, 100 });
		return result;
	}
};

class Codec
{
public:
	static wxImage LoadHEIFImage(const wxString& filepath);
	// Loads HEIF/HEIC/AVIF. An AVIF image sequence yields one frame per sample, anything else a single frame.
	static bool LoadHEIFAnimation(const wxString& filepath, AnimatedImage& out);
	// If `metadata` is non-null, its Exif and XMP are attached to the image through libheif
	// before the file is written (HEIF can't be patched after the fact the way JPEG/PNG can).
	static bool SaveHEIFImage(const wxImage& image, const wxString& filePath, int quality = 80, const MetadataBlobs* metadata = nullptr);
	// Same, but an animation saved to a .avif file is written as an AV1 image sequence (needs libheif 1.20+).
	// The first frame is also stored as the still image, which is where `metadata` goes.
	static bool SaveHEIFImage(const AnimatedImage& animation, const wxString& filePath, int quality = 80, const MetadataBlobs* metadata = nullptr);
	static wxImage DecodeJP2(const unsigned char* data, size_t size);
	static wxImage LoadJP2(const wxString& filePath);
	static bool SaveJP2(const wxImage& img, const wxString& filePath, int quality);
	// Static (single-frame) GIF read/write using giflib. These call giflib directly, so they do not
	// depend on which wxImageHandler wxWidgets picks for GIF.
	static bool SaveGIF(const wxImage& image, const wxString& filePath);
	static wxImage LoadGIF(const wxString& filePath);
	// Animated GIF read/write, also through giflib. Frames come back fully composited.
	static bool SaveGIF(const AnimatedImage& animation, const wxString& filePath);
	static bool LoadGIFAnimation(const wxString& filePath, AnimatedImage& out);
	// Still or animated WebP through libwebp. quality 100 = lossless.
	static bool LoadWebPAnimation(const wxString& filePath, AnimatedImage& out);
	static bool SaveWebPAnimation(const AnimatedImage& animation, const wxString& filePath, int quality = 80);
	// Also registers the giflib handler with wxImage (call right after wxInitAllImageHandlers()).
	static void RegisterGIFHandler();
	static wxImage LoadICNS(const wxString& filePath);
	static bool SaveICNS(const wxImage& img, const wxString& filePath);
};