- **Uploads are sent from the file as they go, not read into memory first.**
  - UltraNet HTTP 0.4.0: `UltraNetHttpRequest::bodyFilePath`, with
    `bodyFileOffset` / `bodyFileLength` for one range of the file. The
    synchronous path streams it through libcurl's read callback, with a seek
    callback for a redirect or an authentication retry, and sends it with a
    Content-Length; the method stays the request's own (a PUT stays a PUT).
    A file that cannot be opened, or a range past its end, fails before
    anything is sent. The async path reads the range into memory.
    `test_body_file.cpp` sends a whole file, a range, a 3 MB file and a file
    that is not there to an echo server on loopback.
  - UltraCloud: a WebDAV / Nextcloud upload held the whole file in memory
    however large it was, and ran under a two-minute whole-transfer limit;
    Dropbox held up to 150 MB, and every upload-session chunk (8 - 10 MB on
    Dropbox, Google Drive and OneDrive) was copied into memory before it was
    sent. All of them now name the file, or the chunk's range of it, instead.
    Google Drive's multipart upload, at most 5 MB, still frames its bytes in
    memory. A whole-file WebDAV PUT and a Dropbox simple upload have no
    whole-transfer limit and a 30-second stall limit. `DropboxProvider::Content`
    gave way to `ContentFromFile`. The provider tests read a body named by
    file through `test_request_body.h`.
