import json
import socket
import threading

SERVER_HOST = "127.0.0.1"
SERVER_PORT = 8888


# ============================
# 工具函数：发一行 JSON / 收一行文本
# ============================
def send_json(sock: socket.socket, data: dict) -> None:
    msg = json.dumps(data) + "\n"
    sock.sendall(msg.encode("utf-8"))


def recv_line(sock: socket.socket):
    """按行读取数据（以 \\n 结束），返回 str 或 None。"""
    buff = bytearray()
    while True:
        ch = sock.recv(1)
        if not ch:
            return None
        buff.extend(ch)
        if ch == b"\n":
            break
    return buff.decode("utf-8").strip()


# ============================
# NebulaChat 客户端封装
# ============================
class NebulaClient:
    def __init__(self, host: str = SERVER_HOST, port: int = SERVER_PORT):
        self.host = host
        self.port = port
        self.sock: socket.socket | None = None

    def connect(self) -> None:
        print(f"[Client] connecting to {self.host}:{self.port} ...")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        print("[Client] connected.")

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            except Exception:
                pass
            self.sock = None

    def send(self, data: dict) -> None:
        if self.sock is None:
            raise RuntimeError("socket not connected")
        send_json(self.sock, data)

    def recv(self):
        if self.sock is None:
            raise RuntimeError("socket not connected")
        line = recv_line(self.sock)
        if line is None:
            return None
        try:
            return json.loads(line)
        except Exception:
            return line

    # ---------- 业务封装 ----------

    # 注册 step1：请求验证码
    def register_send_code(self, phone: str):
        self.send({"cmd": "register", "step": 1, "phone": phone})
        return self.recv()

    # 注册 step2：提交验证码 + 用户名 + 两次密码
    def register_confirm(self, phone: str, code: str,
                         username: str, password: str, password2: str):
        self.send({
            "cmd": "register",
            "step": 2,
            "phone": phone,
            "code": code,
            "user": username,
            "pass": password,
            "pass2": password2,
        })
        return self.recv()

    # 登录：用户名 + 密码
    def login_password(self, username: str, password: str):
        self.send({
            "cmd": "login",
            "mode": "password",
            "user": username,
            "pass": password,
        })
        return self.recv()

    # 登录 step1：手机请求验证码
    def login_sms_send_code(self, phone: str):
        self.send({
            "cmd": "login",
            "mode": "sms",
            "step": 1,
            "phone": phone,
        })
        return self.recv()

    # 登录 step2：手机 + 验证码
    def login_sms_confirm(self, phone: str, code: str):
        self.send({
            "cmd": "login",
            "mode": "sms",
            "step": 2,
            "phone": phone,
            "code": code,
        })
        return self.recv()

    # Echo
    def echo(self, msg: str):
        self.send({"cmd": "echo", "msg": msg})
        return self.recv()

    # Upper
    def upper(self, msg: str):
        self.send({"cmd": "upper", "msg": msg})
        return self.recv()

    # Quit
    def quit(self):
        self.send({"cmd": "quit"})
        return self.recv()


# ============================
# 交互模式
# ============================
def interactive_mode() -> None:
    cli = NebulaClient()
    cli.connect()

    try:
        while True:
            print("\n=== NebulaChat Interactive ===")
            print("1) 注册（手机 + 验证码）")
            print("2) 登录（用户名 + 密码）")
            print("3) 登录（手机 + 验证码）")
            print("4) 发送 echo")
            print("5) 发送 upper")
            print("6) 退出")
            choice = input("选择功能: ").strip()

            if choice == "1":
                phone = input("手机号: ").strip()
                res1 = cli.register_send_code(phone)
                print("[Server register step1]:", res1)

                print("👉 在服务端日志里看验证码，然后在下面输入:")
                code = input("验证码: ").strip()
                user = input("用户名: ").strip()
                pwd1 = input("密码: ").strip()
                pwd2 = input("确认密码: ").strip()
                res2 = cli.register_confirm(phone, code, user, pwd1, pwd2)
                print("[Server register step2]:", res2)

            elif choice == "2":
                user = input("用户名: ").strip()
                pwd = input("密码: ").strip()
                res = cli.login_password(user, pwd)
                print("[Server login(password)]:", res)

            elif choice == "3":
                phone = input("手机号: ").strip()
                res1 = cli.login_sms_send_code(phone)
                print("[Server login sms step1]:", res1)

                print("👉 在服务端日志里看验证码，然后在下面输入:")
                code = input("验证码: ").strip()
                res2 = cli.login_sms_confirm(phone, code)
                print("[Server login sms step2]:", res2)

            elif choice == "4":
                msg = input("echo 内容: ").strip()
                res = cli.echo(msg)
                print("[Server echo]:", res)

            elif choice == "5":
                msg = input("upper 内容: ").strip()
                res = cli.upper(msg)
                print("[Server upper]:", res)

            elif choice == "6":
                res = cli.quit()
                print("[Server quit]:", res)
                break

            else:
                print("无效选择，请重试。")

    finally:
        cli.close()


# ============================
# 简单压测：多线程 echo
# ============================
def stress_test(thread_count: int = 10, msg: str = "hello") -> None:
    def worker(index: int) -> None:
        cli = NebulaClient()
        cli.connect()
        login_res = cli.login_password("Elias", "1234")
        print(f"[Thread {index}] login:", login_res)

        for i in range(20):
            cli.echo(f"{msg}-{index}-{i}")
            cli.recv()  # 丢弃返回值，只要服务器能正常响应即可

        cli.quit()
        cli.close()

    threads: list[threading.Thread] = []
    for i in range(thread_count):
        t = threading.Thread(target=worker, args=(i,))
        t.start()
        threads.append(t)

    for t in threads:
        t.join()

    print("[Stress Test] completed.")


# ============================
# 入口
# ============================
if __name__ == "__main__":
    print("=== NebulaChat Python Test Client ===")
    print("1) 交互模式")
    print("2) 压力测试（多线程 echo）")
    print("3) 退出")
    choice = input("选择模式: ").strip()

    if choice == "1":
        interactive_mode()
    elif choice == "2":
        stress_test(10)
    else:
        print("Bye.")
