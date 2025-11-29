# redis_client.h整体流程图

## 1. RedisClient 接口功能总览图

```mermaid
graph TD
    A[RedisClient 抽象接口] --> B[set<br/>设置字符串键值 可选 TTL]
    A --> C[setNX<br/>仅在 key 不存在时写入]
    A --> D[get<br/>获取字符串键值]
    A --> E[del<br/>删除给定 key]
    A --> F[expire<br/>为已有 key 设置过期时间]
    A --> G[incrBy<br/>对整数值做自增或自减]
    A --> H[eval<br/>执行 Lua 脚本]

    B --> B1[SET key value<br/>或 SET key value EX ttl]
    C --> C1[SET key value NX EX ttlSeconds<br/>常用于分布式锁]
    D --> D1[存在返回 string 不存在返回 nullopt]
    E --> E1[返回删除的 key 数量 一般 0 或 1]
    F --> F1[EXPIRE key seconds<br/>成功返回 true]
    G --> G1[INCRBY key delta<br/>delta 可为负表示自减]
    H --> H1[EVAL script numKeys keys args<br/>用于复杂原子操作]
```

> 如果 `<br/>` 在你 Typora 里解析不爽，可以直接改成纯中文，比如 `set\n设置字符串键值可选TTL`，或者干脆删掉 `<br/>`。

------

## 2. `set` 调用流程（带可选 TTL）

```mermaid
graph TD
    A[调用 set] --> B{ttl 是否有值?}

    B -- 否 --> C[发送命令 SET key value]
    C --> D[写入为持久 key 不设置过期时间]

    B -- 是 --> E[发送命令 SET key value EX ttl]
    E --> F[Redis 内部记录 TTL 到期自动删除 key]
```

------

## 3. `setNX` 调用流程（常用于分布式锁）

```mermaid
graph TD
    A[调用 setNX] --> B[发送 SET key value NX EX ttlSeconds]
    B --> C{Redis 中 key 是否存在?}

    C -- 不存在 --> D[创建新 key 写入 value 并设置 TTL]
    D --> E[返回 成功 写入成功]

    C -- 已存在 --> F[不修改现有 key]
    F --> G[返回 失败 key 已存在]
```

> 实际代码里你返回值是 `void`，具体成功失败要看实现怎么设计（比如抛异常、或另外提供返回 bool 的变体）。图里是“逻辑语义”。

------

## 4. `get` 调用流程

```mermaid
graph TD
    A[调用 get] --> B[发送 GET key]
    B --> C{Redis 中 key 存在?}

    C -- 否 --> D[返回 std::nullopt]
    C -- 是 --> E[返回 std::string 对应的值]
```

------

## 5. `del` + `expire` + `incrBy` 简单图（可以合在一块放）

```mermaid
graph TD
    A[调用 del] --> B[发送 DEL key]
    B --> C[返回删除的 key 数量]

    D[调用 expire] --> E[发送 EXPIRE key seconds]
    E --> F{设置成功?}
    F -- 是 --> G[返回 true]
    F -- 否 --> H[返回 false 例如 key 不存在]

    I[调用 incrBy] --> J[发送 INCRBY key delta]
    J --> K[Redis 内部将原值加上 delta]
    K --> L[返回自增或自减后的最新值]
```

------

## 6.eval`（Lua 脚本）调用流程

```mermaid
graph TD
    A[调用 eval] --> B[准备 script 字符串]
    B --> C[准备 keys 列表 作为 KEYS 传入]
    C --> D[准备 args 列表 作为 ARGV 传入]
    D --> E[发送 EVAL script numKeys keys args]
    E --> F[Redis 执行 Lua 脚本]
    F --> G[脚本以整数形式返回结果]
    G --> H[返回 long long 类型结果给调用方]
```

