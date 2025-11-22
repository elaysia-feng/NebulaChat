import asyncio
import json
import time

HOST = "127.0.0.1"
PORT = 8888

# 每个连接默认发送的请求数
REQUEST_PER_CLIENT = 200


# ============================
# 工具函数：发一行 JSON / 收一行 JSON
# ============================
async def send_json_line(writer: asyncio.StreamWriter, data: dict) -> None:
    """按行发送 JSON 协议：一行一条消息，以 \n 结尾。"""
    msg = json.dumps(data) + "\n"
    writer.write(msg.encode("utf-8"))
    await writer.drain()


async def recv_json_line(reader: asyncio.StreamReader):
    """读取一行并尝试解析为 JSON，失败则返回原始字符串。"""
    line = await reader.readline()
    if not line:
        return None
    text = line.decode("utf-8").strip()
    try:
        return json.loads(text)
    except Exception:
        return text


# ============================
# 单个压测客户端
# ============================
async def run_client(index: int, user: str, passwd: str) -> None:
    """
    1) 建立连接
    2) 用户名 + 密码登录
    3) 连续发送 echo
    """
    try:
        reader, writer = await asyncio.open_connection(HOST, PORT)

        # 1. 登录
        login_req = {
            "cmd": "login",
            "mode": "password",
            "user": user,
            "pass": passwd,
        }
        await send_json_line(writer, login_req)
        login_resp = await recv_json_line(reader)
        # 如需调试可以打开：
        # print(f"[client {index}] login resp:", login_resp)

        # 2. 发送 echo
        for i in range(REQUEST_PER_CLIENT):
            req = {"cmd": "echo", "msg": f"hello-{index}-{i}"}
            await send_json_line(writer, req)
            resp = await recv_json_line(reader)
            if resp is None:
                print(f"[client {index}] server closed, i={i}")
                break

        # 3. 通知 quit（可选）
        try:
            await send_json_line(writer, {"cmd": "quit"})
            await recv_json_line(reader)
        except Exception:
            pass

        writer.close()
        await writer.wait_closed()

    except Exception as e:
        print(f"[client {index}] error: {e}")


# ============================
# 主流程：读配置 + 启动压测
# ============================
async def main() -> None:
    global REQUEST_PER_CLIENT

    try:
        clients = int(input("并发连接数（建议 100~2000）：").strip())
    except ValueError:
        clients = 100

    try:
        per_client = int(input("每个连接发送请求数（默认 200）：").strip() or "200")
    except ValueError:
        per_client = 200

    REQUEST_PER_CLIENT = per_client

    user = input("压测用户名（默认 Elias）：").strip() or "Elias"
    passwd = input("压测密码（默认 1234）：").strip() or "1234"

    total = clients * REQUEST_PER_CLIENT

    print(f"\n[AsyncBench] {clients} clients, {REQUEST_PER_CLIENT} req/client")
    print(f"目标总请求数: {total}")

    start = time.time()
    tasks = [asyncio.create_task(run_client(i, user, passwd))
             for i in range(clients)]
    await asyncio.gather(*tasks)
    cost = time.time() - start

    print("\n===== 压测完成 =====")
    print(f"总请求数: {total}")
    print(f"总耗时: {cost:.3f} s")
    if cost > 0:
        print(f"QPS: {total / cost:.2f}")
    else:
        print("QPS: 耗时太小，无法计算")


if __name__ == "__main__":
    asyncio.run(main())
