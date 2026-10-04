#pragma once

// Shared transport ceiling, independent from the decoded text/cue memory cap.
// Grow on demand; never preallocate the entire ceiling per rendition.
#define SUBTITLE_DOWNLOAD_MAX (8u * 1024u * 1024u)
