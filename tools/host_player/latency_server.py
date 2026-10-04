#!/usr/bin/env python3
"""Servidor HTTP/1.1 com Range, latencia de primeiro byte e banda limitada,
para imitar R2/CDN. Registra cada requisicao com tempo e bytes."""
import argparse, os, random, re, sys, time, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
p = argparse.ArgumentParser()
p.add_argument('--root'); p.add_argument('--port', type=int, default=8765)
p.add_argument('--delay-ms', type=float, default=200); p.add_argument('--jitter-ms', type=float, default=100)
p.add_argument('--mbps', type=float, default=30); p.add_argument('--log'); p.add_argument('--sub-kbps', type=float, default=0); p.add_argument('--tls')
p.add_argument('--log-query', action='store_true')
# Falha transitoria: pedidos cujo caminho contem --fail-match, do N-esimo (1 = primeiro)
# ate N+count-1, recebem 503. Simula queda de rede justamente na legenda.
p.add_argument('--fail-match'); p.add_argument('--fail-from', type=int, default=2); p.add_argument('--fail-count', type=int, default=1)  # registra a query (ex.: token herdado)
a = p.parse_args()
T0 = time.time(); lock = threading.Lock()
logf = open(a.log, 'a') if a.log else sys.stderr
def log(msg):
    with lock: logf.write(f"{time.time()-T0:8.3f} {msg}\n"); logf.flush()
class H(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *x): pass
    def do_HEAD(self): self.serve(head=True)
    def do_GET(self): self.serve(head=False)
    def serve(self, head):
        t = time.time()
        if self.path.startswith('/redir/'):  # imita /api/play -> URL assinada no R2
            self.send_response(302); self.send_header('Location', '/r2/' + self.path[7:])
            self.send_header('Content-Length', '0'); self.end_headers()
            log(f"302 {self.path if a.log_query else self.path.split('?')[0]}"); return
        if a.fail_match and a.fail_match in self.path:
            with lock:
                H.fail_seen = getattr(H, 'fail_seen', 0) + 1; nth = H.fail_seen
            if a.fail_from <= nth < a.fail_from + a.fail_count:
                self.send_response(503); self.send_header('Content-Length', '0'); self.end_headers()
                log(f"503 {self.path.split('?')[0]} (falha simulada {nth})"); return
        path = os.path.join(a.root, self.path.split('?')[0].lstrip('/'))
        if not os.path.isfile(path):
            self.send_response(404); self.send_header('Content-Length', '0'); self.end_headers(); log(f"404 {self.path}"); return
        size = os.path.getsize(path); start, end = 0, size - 1; code = 200
        rng = self.headers.get('Range')
        if rng:
            m = re.match(r'bytes=(\d*)-(\d*)', rng)
            if m:
                if m.group(1): start = int(m.group(1))
                if m.group(2): end = min(int(m.group(2)), size - 1)
                if start >= size:
                    self.send_response(416); self.send_header('Content-Range', f'bytes */{size}'); self.send_header('Content-Length','0'); self.end_headers(); return
                code = 206
        time.sleep(max(0, (a.delay_ms + random.uniform(-a.jitter_ms, a.jitter_ms)) / 1000))
        n = end - start + 1
        self.send_response(code)
        ctype = 'application/vnd.apple.mpegurl' if path.endswith('.m3u8') else 'text/vtt' if path.endswith('.vtt') else 'application/json' if path.endswith('/probe') else 'video/mp4'
        self.send_header('Content-Type', ctype); self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Content-Length', str(n))
        if code == 206: self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
        self.end_headers()
        sent = 0
        if not head:
            rate = a.mbps * 1e6 / 8; chunk = 64 * 1024; t1 = time.time()
            if a.sub_kbps and '/subtitles/' in self.path: rate = a.sub_kbps * 1024; chunk = 512
            try:
                with open(path, 'rb') as f:
                    f.seek(start)
                    while sent < n:
                        buf = f.read(min(chunk, n - sent))
                        if not buf: break
                        self.wfile.write(buf); sent += len(buf)
                        ahead = sent / rate - (time.time() - t1)
                        if ahead > 0: time.sleep(ahead)
            except (BrokenPipeError, ConnectionResetError):
                log(f"ABORT {self.path.split('?')[0]} {start}-{end} sent={sent}"); return
        shown = self.path if a.log_query else self.path.split('?')[0]
        log(f"{code} {shown} {start}-{end} bytes={sent} ms={(time.time()-t)*1000:.0f} conn={self.client_address[1]}")
ThreadingHTTPServer.daemon_threads = True
srv = ThreadingHTTPServer(('127.0.0.1', a.port), H)
if a.tls:
    import ssl
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(a.tls + '.crt', a.tls + '.key')
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
