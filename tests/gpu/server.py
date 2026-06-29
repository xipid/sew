import http.server, mimetypes
mimetypes.add_type('application/javascript', '.ts')
http.server.test(HandlerClass=http.server.SimpleHTTPRequestHandler)