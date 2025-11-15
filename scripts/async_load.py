import asyncio
import json
import time

HOST = "127.0.0.1"
PORT = 8888

# 每个连接发送多少次消息
REQUEST_PER_CLIENT = 200

async def run_client(index):
    try:
        reader, writer = await asyncio.open_connection(HOST, PORT)
        # login
        req = {"cmd": "login", "user": "Elias", "pass": "1234"}
        writer.write((json.dumps(req) + "\n").encode())
        await writer.drain()
        await reader.readline()

        for i in range(REQUEST_PER_CLIENT):
            req = {"cmd": "echo", "msg": f"hello-{index}-{i}"}
            writer.write((json.dumps(req) + "\n").encode())
            await writer.drain()
            await reader.readline()

        writer.close()
        await writer.wait_closed()
    except Exception as e:
        print(f"[client {index}] error: {e}")

async def main():
    CLIENTS = int(input("并发连接数（推荐 1000 到 5000）："))
    print(f"[AsyncTest] starting with {CLIENTS} clients...")

    tasks = []
    start = time.time()

    for i in range(CLIENTS):
        tasks.append(asyncio.create_task(run_client(i)))

    await asyncio.gather(*tasks)
    end = time.time()

    total = CLIENTS * REQUEST_PER_CLIENT
    print(f"\n===== 压测完成 =====")
    print(f"总请求数: {total}")
    print(f"总耗时: {end - start:.3f}s")
    print(f"QPS: {total / (end - start):.2f}")

if __name__ == "__main__":
    asyncio.run(main())
