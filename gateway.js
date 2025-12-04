// 需要先安装依赖：
//   npm init -y
//   npm install ws

const WebSocket = require('ws');
const net = require('net');

const WS_PORT = 9000;      // 浏览器连的是这个端口 (ws://127.0.0.1:9000/ws)
const TCP_HOST = '127.0.0.1';
const TCP_PORT = 8888;     // 你的 C++ 聊天服务器端口

const wss = new WebSocket.Server({ port: WS_PORT, path: '/ws' });

console.log(`NebulaChat 网关已启动：ws://127.0.0.1:${WS_PORT}/ws -> tcp://${TCP_HOST}:${TCP_PORT}`);

wss.on('connection', (ws) => {
  console.log('WebSocket 客户端已连接');

  // 与 C++ 服务器建立一个 TCP 连接
  const tcp = net.createConnection({ host: TCP_HOST, port: TCP_PORT }, () => {
    console.log('已连接 C++ 聊天服务器');
  });

  // WebSocket 收到消息 -> 直接转发给 TCP
  ws.on('message', (data) => {
    // data 是 Buffer 或字符串，这里统一转成字符串
    const text = data.toString();
    // 确保每条消息后面有换行（你的 C++ 端按行解析）
    if (!text.endsWith('\n')) {
      tcp.write(text + '\n');
    } else {
      tcp.write(text);
    }
  });

  // TCP 收到数据 -> 按行转发给 WebSocket
  let buffer = '';
  tcp.on('data', (chunk) => {
    buffer += chunk.toString();
    let idx;
    while ((idx = buffer.indexOf('\n')) >= 0) {
      const line = buffer.slice(0, idx);
      buffer = buffer.slice(idx + 1);
      if (line.trim().length > 0) {
        ws.send(line);
      }
    }
  });

  tcp.on('error', (err) => {
    console.error('TCP 连接错误:', err.message);
    ws.close();
  });

  tcp.on('close', () => {
    console.log('TCP 连接已关闭');
    ws.close();
  });

  ws.on('close', () => {
    console.log('WebSocket 已关闭');
    tcp.end();
  });

  ws.on('error', (err) => {
    console.error('WebSocket 错误:', err.message);
    tcp.end();
  });
});
