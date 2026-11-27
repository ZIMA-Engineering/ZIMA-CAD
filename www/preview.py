"""Serve public build outputs locally and rebuild when authored inputs change."""
import argparse
import http.server
import importlib
import threading
import time
from pathlib import Path
from urllib.parse import unquote, urlsplit

import build as generator
ROOT, WEB = generator.ROOT, generator.WEB


def signature():
    files = [WEB / "build.py", ROOT / "config/localization/metadata.ini", WEB / "0000-index/metadata.ini", WEB / "0300-documentation/0000-index/metadata.ini"]
    for directory in (WEB / "_source", WEB / "0200-gallery/images", ROOT / "doc/guides", ROOT / "resources/branding"):
        files.extend(path for path in directory.rglob("*") if path.is_file() and path.name != "outputs.json")
    return [(str(path), path.stat().st_mtime_ns, path.stat().st_size) for path in sorted(files) if path.exists()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    outputs = generator.build()
    revision = 0
    lock = threading.Lock()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            path = unquote(urlsplit(self.path).path).lstrip("/") or "index.html"
            with lock:
                data = str(revision).encode() if path == "__revision" else outputs.get(path)
            if data is None:
                self.send_error(404)
                return
            import mimetypes
            if path.endswith('.html'):
                script = ('<script>let revision=' + str(revision) + ';setInterval(async()=>{try{const next=await fetch("/__revision").then(r=>r.text());if(Number(next)!==revision)location.reload()}catch{}},1500)</script>')
                data = data.replace(b'</body>', script.encode() + b'</body>')
            self.send_response(200)
            self.send_header("Content-Type", mimetypes.guess_type(path)[0] or "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, *_):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    def watch():
        nonlocal outputs, revision
        previous = signature()
        while True:
            time.sleep(1)
            current = signature()
            if current != previous:
                try:
                    updated = importlib.reload(generator).build()
                    with lock:
                        outputs = updated
                        revision += 1
                    previous = current
                except Exception as error:
                    print(f"Build deferred: {error}", flush=True)
    threading.Thread(target=watch, daemon=True).start()
    print(f"Local: http://127.0.0.1:{args.port}/0000-index/index_cs.html", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
