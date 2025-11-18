import socket
import json
import threading

SERVER_HOST = "127.0.0.1"
SERVER_PORT = 8888

# =====================================================
# 工具：发送 JSON 并带换行
# =====================================================
def send_json(sock, data: dict):
    msg = json.dumps(data) + "\n"
    sock.sendall(msg.encode("utf-8"))


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
    return buff.decode("utf-8").strip()


# =====================================================
# NebulaChat 客户端封装
# =====================================================
class NebulaClient:
    def __init__(self, host=SERVER_HOST, port=SERVER_PORT):
        self.host = host
        self.port = port
        self.sock = None

    # 连接服务端
    def connect(self):
        print(f"[Client] connecting to {self.host}:{self.port} ...")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        print("[Client] connected.")

    # 关闭连接
    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except:
                pass
            self.sock = None

    # 发送 JSON
    def send(self, data: dict):
        send_json(self.sock, data)

    # 接收 server 响应
    def recv(self):
        res = recv_line(self.sock)
        if res is None:
            return None
        try:
            return json.loads(res)
        except Exception:
            return res

    # ============================
    # 注册：step1 发送验证码
    # ============================
    def register_send_code(self, phone: str):
        payload = {
            "cmd": "register",
            "step": 1,
            "phone": phone
        }
        self.send(payload)
        return self.recv()

    # ============================
    # 注册：step2 提交验证码 + 用户名 + 两次密码
    # ============================
    def register_confirm(self, phone: str, code: str,
                         username: str, password: str, password2: str):
        payload = {
            "cmd": "register",
            "step": 2,
            "phone": phone,
            "code": code,
            "user": username,
            "pass": password,
            "pass2": password2
        }
        self.send(payload)
        return self.recv()

    # ============================
    # 登录：用户名 + 密码
    # ============================
    def login_password(self, username: str, password: str):
        payload = {
            "cmd": "login",
            "mode": "password",
            "user": username,
            "pass": password
        }
        self.send(payload)
        return self.recv()

    # ============================
    # 登录：step1 手机号请求验证码
    # ============================
    def login_sms_send_code(self, phone: str):
        payload = {
            "cmd": "login",
            "mode": "sms",
            "step": 1,
            "phone": phone
        }
        self.send(payload)
        return self.recv()

    # ============================
    # 登录：step2 手机号 + 验证码登录
    # ============================
    def login_sms_confirm(self, phone: str, code: str):
        payload = {
            "cmd": "login",
            "mode": "sms",
            "step": 2,
            "phone": phone,
            "code": code
        }
        self.send(payload)
        return self.recv()

    # ============================
    # Echo
    # ============================
    def echo(self, msg: str):
        self.send({"cmd": "echo", "msg": msg})
        return self.recv()

    # ============================
    # Upper
    # ============================
    def upper(self, msg: str):
        self.send({"cmd": "upper", "msg": msg})
        return self.recv()

    # ============================
    # Quit
    # ============================
    def quit(self):
        self.send({"cmd": "quit"})
        return self.recv()


# =====================================================
# 交互模式：菜单式
# =====================================================
def interactive_mode():
    cli = NebulaClient()
    cli.connect()

    authed = False

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
            # 注册流程
            phone = input("请输入手机号: ").strip()
            res = cli.register_send_code(phone)
            print("[Server register step1]:", res)

            print("👉 查看服务端日志中的验证码，然后在这里输入：")
            code = input("请输入短信验证码: ").strip()
            user = input("请输入用户名: ").strip()
            pwd1 = input("请输入密码: ").strip()
            pwd2 = input("请再次输入密码: ").strip()

            res2 = cli.register_confirm(phone, code, user, pwd1, pwd2)
            print("[Server register step2]:", res2)

        elif choice == "2":
            # 用户名密码登录
            user = input("用户名: ").strip()
            pwd  = input("密码: ").strip()
            res  = cli.login_password(user, pwd)
            print("[Server login(password)]:", res)
            if isinstance(res, dict) and res.get("ok"):
                authed = True

        elif choice == "3":
            # 手机验证码登录
            phone = input("手机号: ").strip()
            res1  = cli.login_sms_send_code(phone)
            print("[Server login sms step1]:", res1)

            print("👉 查看服务端日志中的验证码，然后在这里输入：")
            code = input("请输入短信验证码: ").strip()
            res2 = cli.login_sms_confirm(phone, code)
            print("[Server login sms step2]:", res2)
            if isinstance(res2, dict) and res2.get("ok"):
                authed = True

        elif choice == "4":
            msg = input("echo 内容: ")
            res = cli.echo(msg)
            print("[Server echo]:", res)

        elif choice == "5":
            msg = input("upper 内容: ")
            res = cli.upper(msg)
            print("[Server upper]:", res)

        elif choice == "6":
            res = cli.quit()
            print("[Server quit]:", res)
            break

        else:
            print("无效选择，请重试。")

    cli.close()


# =====================================================
# 压测：多线程发送 echo（使用用户名密码登录）
# =====================================================
def stress_test(thread_count=10, msg="hello"):
    def worker(index):
        cli = NebulaClient()
        cli.connect()
        # 这里假设已有一个固定用户
        login_res = cli.login_password("Elias", "1234")
        print(f"[Thread {index}] login result:", login_res)

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
    print("1) 交互模式（推荐，用来测注册/登录）")
    print("2) 压力测试（多线程 echo）")
    print("3) 退出")
    choice = input("选择模式: ").strip()

    if choice == "1":
        interactive_mode()
    elif choice == "2":
        stress_test(10)
    else:
        print("Bye!")
