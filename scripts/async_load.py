import asyncio
import json
import time

HOST = "127.0.0.1"
PORT = 8888

# ============================
# 工具函数：发一行 JSON，收一行 JSON
# ============================

async def send_json_line(writer: asyncio.StreamWriter, data: dict):
    msg = json.dumps(data) + "\n"
    writer.write(msg.encode("utf-8"))
    await writer.drain()

async def recv_json_line(reader: asyncio.StreamReader):
    line = await reader.readline()
    if not line:
        return None
    text = line.decode("utf-8").strip()
    try:
        return json.loads(text)
    except Exception:
        return text


# 每个连接发送多少条请求（默认值，可在 main 里改）
REQUEST_PER_CLIENT = 200


async def run_client(index: int, user: str, passwd: str):
    """
    单个压测客户端：
    1) 建立连接
    2) 用户名+密码登录
    3) 连续发送 echo
    """
    try:
        reader, writer = await asyncio.open_connection(HOST, PORT)

        # 1. 登录（用户名 + 密码）
        login_req = {
            "cmd":  "login",
            "mode": "password",  # 和服务端协议对齐
            "user": user,
            "pass": passwd,
        }
        await send_json_line(writer, login_req)
        login_resp = await recv_json_line(reader)
        # 不强制要求登录成功，但打印一下错误信息有帮助
        # 可以按需打开下面的打印
        # print(f"[client {index}] login resp:", login_resp)

        # 2. 循环发送 echo
        for i in range(REQUEST_PER_CLIENT):
            req = {
                "cmd": "echo",
                "msg": f"hello-{index}-{i}"
            }
            await send_json_line(writer, req)
            resp = await recv_json_line(reader)
            if resp is None:
                print(f"[client {index}] server closed while echo, i={i}")
                break
            # 不打印 resp，避免 IO 把压测本身拖慢
            # 如需调试可以解开：
            # if i == 0:
            #     print(f"[client {index}] first echo resp:", resp)

        # 3. 发送 quit（可选）
        try:
            await send_json_line(writer, {"cmd": "quit"})
            await recv_json_line(reader)
        except Exception:
            pass

        writer.close()
        await writer.wait_closed()

    except Exception as e:
        print(f"[client {index}] error: {e}")


async def main():
    global REQUEST_PER_CLIENT

    try:
        clients = int(input("并发连接数（建议 100~2000，看机器情况）：").strip())
    except ValueError:
        clients = 100

    try:
        per_client = int(input("每个连接发送多少次请求（默认 200）：").strip() or "200")
    except ValueError:
        per_client = 200

    REQUEST_PER_CLIENT = per_client

    user = input("压测使用的用户名（默认 Elias）：").strip() or "Elias"
    passwd = input("压测使用的密码（默认 1234）：").strip() or "1234"

    print(f"\n[AsyncTest] starting with {clients} clients, "
          f"{REQUEST_PER_CLIENT} requests per client...")
    print(f"目标总请求数: {clients * REQUEST_PER_CLIENT}")

    tasks = []
    start = time.time()

    for i in range(clients):
        tasks.append(asyncio.create_task(run_client(i, user, passwd)))

    await asyncio.gather(*tasks)
    end = time.time()

    total = clients * REQUEST_PER_CLIENT
    cost = end - start
    print("\n===== 压测完成 =====")
    print(f"总请求数: {total}")
    print(f"总耗时: {cost:.3f} s")
    if cost > 0:
        print(f"QPS: {total / cost:.2f}")
    else:
        print("QPS: 无法计算（耗时太小）")


if __name__ == "__main__":
    asyncio.run(main())
