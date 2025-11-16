import socket
import json
import threading
import time

SERVER_HOST = "127.0.0.1"
SERVER_PORT = 8888

# =====================================================
# 工具：发送 JSON 并带换行
# =====================================================
def send_json(sock, data: dict):
    msg = json.dumps(data) + "\n"
    sock.sendall(msg.encode())


# =====================================================
# 工具：接收一行（你的协议是按行 \n 拆包）
# =====================================================
def recv_line(sock):
    buff = b""
    while True:
        ch = sock.recv(1)
        if not ch:
            return None
        buff += ch
        if ch == b"\n":
            break
    return buff.decode().strip()


# =====================================================
# 自动登录客户端
# =====================================================
class NebulaClient:
    def __init__(self, host=SERVER_HOST, port=SERVER_PORT):
        self.host = host
        self.port = port
        self.sock = None

    def connect(self):
        print(f"[Client] connecting to {self.host}:{self.port} ...")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        print("[Client] connected.")

    def close(self):
        if self.sock:
            self.sock.close()

    # 发送指令
    def send(self, data: dict):
        send_json(self.sock, data)

    # 接收 server 响应
    def recv(self):
        res = recv_line(self.sock)
        if res:
            try:
                return json.loads(res)
            except:
                return res
        return None

    # ============================
    # 注册
    # ============================
    def register(self, user, passwd):
        self.send({"cmd": "register", "user": user, "pass": passwd})
        return self.recv()

    # ============================
    # 登录
    # ============================
    def login(self, user, passwd):
        self.send({"cmd": "login", "user": user, "pass": passwd})
        return self.recv()

    # ============================
    # Echo
    # ============================
    def echo(self, msg):
        self.send({"cmd": "echo", "msg": msg})
        return self.recv()

    # ============================
    # Upper
    # ============================
    def upper(self, msg):
        self.send({"cmd": "upper", "msg": msg})
        return self.recv()

    # ============================
    # Quit
    # ============================
    def quit(self):
        self.send({"cmd": "quit"})
        return self.recv()


# =====================================================
# 简单交互测试（类似 nc，但更好）
# =====================================================
def interactive_mode():
    cli = NebulaClient()
    cli.connect()

    print("\n>>> 输入 JSON 或命令（exit 退出）")
    while True:
        text = input(">>> ")
        if text == "exit":
            break

        # 如果是纯命令，例如 echo hello
        if text.startswith("echo "):
            msg = text.split(" ", 1)[1]
            cli.send({"cmd": "echo", "msg": msg})
        else:
            # 当 JSON 发送
            try:
                data = json.loads(text)
                cli.send(data)
            except:
                print("[Error] 请输入有效 JSON 或指令")
                continue

        print("[Server]:", cli.recv())

    cli.quit()
    cli.close()


# =====================================================
# 压测：多线程大量发送 echo
# =====================================================
def stress_test(thread_count=10, msg="hello"):
    def worker(index):
        cli = NebulaClient()
        cli.connect()
        cli.login("Elias", "1234")  # 可换
        for i in range(20):
            cli.echo(f"{msg}-{index}-{i}")
            cli.recv()
        cli.quit()
        cli.close()

    ths = []
    for i in range(thread_count):
        th = threading.Thread(target=worker, args=(i,))
        th.start()
        ths.append(th)

    for th in ths:
        th.join()

    print("[Stress Test] Completed!")


# =====================================================
# 主程序入口
# =====================================================
if __name__ == "__main__":
    print("=== NebulaChat Python Test Client ===")
    print("1) 自动注册 + 登录 测试")
    print("2) 手动交互测试")
    print("3) 压力测试（多线程）")
    print("4) 退出")

    choice = input("选择模式: ")

    if choice == "1":
        cli = NebulaClient()
        cli.connect()
        print(cli.register("seele", "seele"))
        print(cli.login("Elias", "1234"))
        print(cli.echo("hello^^"))
        print(cli.upper("abcdefg"))
        print(cli.quit())
        cli.close()

    elif choice == "2":
        interactive_mode()

    elif choice == "3":
        stress_test(10)

    else:
        print("Bye!")
