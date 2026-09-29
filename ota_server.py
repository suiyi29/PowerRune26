from http.server import HTTPServer, SimpleHTTPRequestHandler
import socket

class OTAHandler(SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        # 自定义日志输出
        client_ip = self.client_address[0]
        msg = "%s - %s" % (client_ip, format % args)
        print(msg)

    # 关闭默认控制台输出，改用上面自定义log_message
    def log_request(self, code='-', size='-'):
        self.log_message('"%s" %s %s', self.requestline, str(code), str(size))

def get_local_ip():
    # 获取本机局域网IP
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # 不需要真实连通，用来获取本机网卡IP
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

if __name__ == "__main__":
    HOST = "0.0.0.0"
    PORT = 8000
    server = HTTPServer((HOST, PORT), OTAHandler)
    local_ip = get_local_ip()
    print("=" * 60)
    print(f"OTA HTTP服务器已启动！")
    print(f"本机局域网地址：http://{local_ip}:{PORT}/")
    print(f"本地测试地址：http://127.0.0.1:{PORT}/")
    print(f"将 .bin 固件放在当前脚本所在文件夹！")
    print("按 Ctrl + C 停止服务器")
    print("=" * 60)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n服务器正在关闭...")
        server.shutdown()
        print("服务器已退出")
