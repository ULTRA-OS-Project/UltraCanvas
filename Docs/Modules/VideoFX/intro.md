# VideoFX

The video editing and conversion engine. Probe any media file, pull frames and filmstrips, and build a timeline of clips — each trimmed, sped up or slowed down, colour-graded, faded, cropped or rotated — that is joined and encoded to MP4, WebM, MOV, MKV, an animated GIF or an audio file, with picture and sound kept in sync across every join. FFmpeg does the work behind VideoFX's own API, so no codec library leaks into application code; exports run on a background job with progress and cancel. Stage 1 is implemented; transitions, overlays, keyframes and multi-track mixing are next.
