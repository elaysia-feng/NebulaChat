

# cache_client.h整体结构总览图

- `set`
- `setLogicalExpire`
- `getWithPassThrough`
- `getWithLogicalExpire`
- `submitBackground`

我都用 **中英+简单中文**，不写那种带括号和逗号的一长串函数签名，尽量兼容你 2024 的 Typora。

------

## 1.整体结构总览图：CacheClient 做了啥

```mermaid
graph TD
    A[CacheClient 缓存客户端] --> B[set 物理 TTL 缓存]
    A --> C[setLogicalExpire 逻辑过期写入]
    A --> D[getWithPassThrough 防缓存穿透]
    A --> E[getWithLogicalExpire 防缓存击穿]
    A --> F[submitBackground 提交后台任务]

    B --> B1[直接依赖 Redis TTL 过期删除 key]
    C --> C1[存 data 和 expireAt 不设置 Redis TTL]
    D --> D1[空值缓存 NULL_MARK 加短 TTL 减少打 DB]
    E --> E1[逻辑过期 加 异步重建 热点 key 不打爆 DB]
    F --> F1[优先使用上层线程池 否则新建线程并 detach]
    
    
    
    
```

------

## 2. `set`：物理 TTL 写入流程

```mermaid
graph TD
    A[调用 set] --> B[将 value 转成 Json 对象]
    B --> C[序列化为字符串 j.dump]
    C --> D[调用 redis.set key value ttl]
    D --> E[Redis 使用自身 TTL 机制 过期自动删除 key]
```

------

## 3.`setLogicalExpire`：逻辑过期写入流程

```mermaid
graph TD
    A[调用 setLogicalExpire] --> B[获取当前时间 Clock now]
    B --> C[当前时间 加 logicalTtl 得到 expireTime]
    C --> D[将 expireTime 转成 秒级时间戳 expireSec]

    D --> E[构造 Json 对象]
    E --> F[字段 data 保存业务数据]
    F --> G[字段 expireAt 保存 expireSec]

    G --> H[调用 redis.set key j.dump 不设置 TTL]
    H --> I[由业务代码自己判断是否过期]
```

------

## 4. `getWithPassThrough`：防缓存穿透流程图

```mermaid
graph TD
    A[调用 getWithPassThrough] --> B[Redis 读取 key]
    B --> C{缓存有值?}

    C -- 否 --> D[调用 loader 查询数据库]
    C -- 是 --> E{值是否为空标记 NULL_MARK}

    E -- 是 --> F[返回 std::nullopt 不打 DB]
    E -- 否 --> G[尝试解析 JSON]

    G --> H{解析成功?}
    H -- 是 --> I[反序列化得到 T 并返回]
    H -- 否 --> D[当作未命中 走 DB 流程]

    D --> J{DB 有数据?}
    J -- 否 --> K[写入 NULL_MARK 加 nullTtl]
    K --> L[返回 std::nullopt]

    J -- 是 --> M[将数据转为 Json 并写入 Redis 加 normalTtl]
    M --> N[返回从 DB 得到的 T]
```

------

## 5.`getWithLogicalExpire`：防缓存击穿（逻辑过期 + 异步重建）

这个我帮你 **保留主要逻辑**，异常和兼容旧格式（直接存 T）统一归在一个“处理异常或旧数据”的节点里，图不会太乱，文字可以在报告里展开。

```mermaid
graph TD
    A[调用 getWithLogicalExpire] --> B[Redis 读取 key]
    B --> C{缓存有值?}

    %% 1 缓存完全未命中
    C -- 否 --> D[调用 loader 查询数据库]
    D --> E{DB 有数据?}
    E -- 否 --> F[返回 std::nullopt]
    E -- 是 --> G[调用 setLogicalExpire 写入 data 和 expireAt]
    G --> H[返回从 DB 得到的 data]

    %% 2 缓存命中但 JSON 异常 或 旧格式
    C -- 是 --> I[尝试解析 JSON]
    I --> J{是正常的 data 加 expireAt 结构?}

    J -- 否 --> K[当作异常或旧数据 再次调用 loader]
    K --> L{DB 有数据?}
    L -- 否 --> M[返回 std::nullopt]
    L -- 是 --> N[setLogicalExpire 重建缓存]
    N --> O[返回从 DB 得到的 data]

    %% 3 正常逻辑过期结构
    J -- 是 --> P[从 JSON 中取出 data 和 expireAt]
    P --> Q[计算当前时间 nowSec]
    Q --> R{nowSec 小于 expireAt?}

    R -- 是 --> S[未过期 直接返回 data]
    R -- 否 --> T[已过期 提交后台任务异步重建]
    T --> U[后台任务调用 loader 成功后再次 setLogicalExpire]
    T --> V[当前请求仍旧返回旧的 data 作为兜底]
```



## 6. `submitBackground`：后台任务提交逻辑

```mermaid
graph TD
    A[调用 submitBackground 传入 task] --> B{submitBackground_ 是否存在?}

    B -- 是 --> C[调用 submitBackground_ 把 task 提交给上层线程池或调度器]
    C --> D[函数返回]

    B -- 否 --> E[创建 std::thread th 运行 task]
    E --> F[调用 th.detach 分离线程]
    F --> G[函数返回]
```

------

