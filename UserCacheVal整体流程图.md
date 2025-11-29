# UserCacheVal整体流程图



## 一、CacheClient 相关流程图

### 1. CacheClient 功能总览

```mermaid
graph TD
    A[CacheClient 缓存客户端] --> B[set 物理TTL缓存]
    A --> C[setLogicalExpire 逻辑过期写入]
    A --> D[getWithPassThrough 防缓存穿透]
    A --> E[getWithLogicalExpire 防缓存击穿]
    A --> F[submitBackground 提交后台任务]

    B --> B1[依赖 Redis TTL 到期自动删除 key]
    C --> C1[存 data 和 expireAt 不设置 Redis TTL]
    D --> D1[使用空值标记 NULL_MARK 和短 TTL 减少打 DB]
    E --> E1[逻辑过期加异步重建 保护热点 key]
    F --> F1[优先使用上层线程池 否则新建线程并分离]
```

------

### 2. set 物理 TTL 写入流程

```mermaid
graph TD
    A[调用 set] --> B[把数据转换成 Json 对象]
    B --> C[序列化为字符串 j.dump]
    C --> D[调用 redis.set key value ttl]
    D --> E[Redis 使用自身 TTL 到期删除 key]
```

------

### 3. setLogicalExpire 逻辑过期写入流程

```mermaid
graph TD
    A[调用 setLogicalExpire] --> B[获取当前时间 Clock now]
    B --> C[加上 logicalTtl 得到 expireTime]
    C --> D[转换为秒级时间戳 expireSec]

    D --> E[构造 Json 对象 j]
    E --> F[j 的 data 字段保存业务数据]
    F --> G[j 的 expireAt 字段保存 expireSec]

    G --> H[调用 redis.set key j.dump 不设置 TTL]
    H --> I[是否过期由业务根据 expireAt 判断]
```

------

### 4. getWithPassThrough 防缓存穿透

```mermaid
graph TD
    A[调用 getWithPassThrough] --> B[redis.get 读取 key]
    B --> C{缓存有值}

    C -- 否 --> D[调用 loader 查询数据库]
    C -- 是 --> E{值是否为 NULL_MARK 空值标记}

    E -- 是 --> F[返回 nullopt 不再访问数据库]
    E -- 否 --> G[尝试解析 Json]

    G --> H{解析成功}
    H -- 是 --> I[反序列化为类型 T 并返回]
    H -- 否 --> D[视为未命中 转到查询数据库]

    D --> J{数据库有数据}
    J -- 否 --> K[写入 NULL_MARK 和 nullTtl 作为空值缓存]
    K --> L[返回 nullopt]

    J -- 是 --> M[将数据转 Json 存入 Redis 并设置 normalTtl]
    M --> N[返回从数据库获得的值]
```

------

### 5. getWithLogicalExpire 防缓存击穿

```mermaid
graph TD
    A[调用 getWithLogicalExpire] --> B[redis.get 读取 key]
    B --> C{缓存有值}

    C -- 否 --> D[调用 loader 查询数据库]
    D --> E{数据库有数据}
    E -- 否 --> F[返回 nullopt]
    E -- 是 --> G[调用 setLogicalExpire 写入 data 和 expireAt]
    G --> H[返回从数据库获得的数据]

    C -- 是 --> I[尝试解析 Json 并检查结构]
    I --> J{是否为 data 加 expireAt 结构}

    J -- 否 --> K[视为异常或旧数据 再次调用 loader]
    K --> L{数据库有数据}
    L -- 否 --> M[返回 nullopt]
    L -- 是 --> N[调用 setLogicalExpire 重建缓存]
    N --> O[返回从数据库获得的数据]

    J -- 是 --> P[从 Json 中取出 data 和 expireAt]
    P --> Q[计算当前时间秒数 nowSec]
    Q --> R{nowSec 小于 expireAt}

    R -- 是 --> S[未过期 直接返回 data]
    R -- 否 --> T[已过期 调用 submitBackground 提交异步重建任务]
    T --> U[后台任务中 loader 查询数据库并调用 setLogicalExpire]
    T --> V[当前请求仍返回旧的 data 作为兜底]
```

------

### 6. submitBackground 后台任务提交

```mermaid
graph TD
    A[调用 submitBackground 传入 task] --> B{submitBackground_ 是否存在}

    B -- 是 --> C[调用 submitBackground_ 交由上层线程池或调度器执行]
    C --> D[函数返回]

    B -- 否 --> E[创建 std::thread 执行 task]
    E --> F[调用 detach 分离线程]
    F --> G[函数返回]
```

------

## 二、RedisClient 相关流程图

### 1. RedisClient 接口总览

```mermaid
graph TD
    A[RedisClient 抽象接口] --> B[set 设置字符串键值 可选 TTL]
    A --> C[setNX 仅在 key 不存在时写入]
    A --> D[get 获取字符串值]
    A --> E[del 删除 key]
    A --> F[expire 设置过期时间]
    A --> G[incrBy 整数自增或自减]
    A --> H[eval 执行 Lua 脚本]

    B --> B1[对应 SET key value 或 SET key value EX ttl]
    C --> C1[对应 SET key value NX EX ttlSeconds]
    D --> D1[key 存在返回字符串 否则返回 nullopt]
    E --> E1[返回实际删除的 key 数量]
    F --> F1[对应 EXPIRE key seconds 成功返回 true]
    G --> G1[对应 INCRBY key delta delta 可以为负]
    H --> H1[对应 EVAL script numKeys keys args]
```

------

### 2. set 调用流程

```mermaid
graph TD
    A[调用 set] --> B{ttl 是否有值}

    B -- 否 --> C[发送 SET key value]
    C --> D[写入持久 key 不设置过期时间]

    B -- 是 --> E[发送 SET key value EX ttl]
    E --> F[Redis 记录 TTL 到期自动删除 key]
```

------

### 3. setNX 调用语义

```mermaid
graph TD
    A[调用 setNX] --> B[发送 SET key value NX EX ttlSeconds]
    B --> C{原来是否存在 key}

    C -- 否 --> D[创建新 key 写入 value 并设置 TTL]
    D --> E[表示抢占成功或初始化成功]

    C -- 是 --> F[不修改已有 key]
    F --> G[表示抢占失败或已有数据]
```

------

### 4. get del expire incrBy eval 简略流程

```mermaid
graph TD
    A[调用 get] --> B[发送 GET key]
    B --> C{key 是否存在}
    C -- 否 --> D[返回 nullopt]
    C -- 是 --> E[返回字符串值]

    F[调用 del] --> G[发送 DEL key]
    G --> H[返回删除的 key 数量]

    I[调用 expire] --> J[发送 EXPIRE key seconds]
    J --> K{设置成功}
    K -- 是 --> L[返回 true]
    K -- 否 --> M[返回 false 例如 key 不存在]

    N[调用 incrBy] --> O[发送 INCRBY key delta]
    O --> P[Redis 内部将原值加上 delta]
    P --> Q[返回新的值]

    R[调用 eval] --> S[发送 EVAL script numKeys keys args]
    S --> T[Redis 执行 Lua 脚本并计算结果]
    T --> U[将结果转换为 long long 返回]
```

------

## 三、LocalUserByName 本地用户名缓存

### 1. 结构总览

```mermaid
graph TD
    A[LocalUserByName 本地用户名缓存] --> B[LRU 链表 list Node]
    A --> C[哈希索引 unordered_map username 到迭代器]
    A --> D[互斥锁 mutex 保护并发访问]
    A --> E[每个 Node 包含 username id password isNull expire]
```

------

### 2. get 按用户名读取

```mermaid
graph TD
    A[调用 LocalUserByName get] --> B[加锁 lock_guard]
    B --> C[在 index 中查找 username]

    C --> D{是否找到}
    D -- 否 --> E[返回 false 表示本地未命中]

    D -- 是 --> F[取 Node 引用]
    F --> G[获取当前时间 now]
    G --> H{now 是否大于等于 node.expire}

    H -- 是 --> I[从 cache 链表中删除该节点]
    I --> J[从 index 中删除该用户名]
    J --> K[返回 false 视为已过期未命中]

    H -- 否 --> L[LRU 更新 将节点移动到链表尾部]
    L --> M[填充 idOut passwordOut isNullOut]
    M --> N[返回 true 表示命中]
```

------

### 3. put 写入正常用户数据

```mermaid
graph TD
    A[调用 LocalUserByName put] --> B[加锁]
    B --> C[在 index 中查找 username]

    C --> D{是否已存在}
    D -- 是 --> E[更新 Node 的 id password isNull 为 false]
    E --> F[更新过期时间为 now 加 ttlSeconds]
    F --> G[LRU 更新 将节点移动到链表尾部]
    G --> H[返回]

    D -- 否 --> I{index 大小是否达到 capacity}
    I -- 是 --> J[取 cache 链表头部最久未用节点]
    J --> K[从 index 删除最旧用户名]
    K --> L[cache 弹出头部节点]

    I --> M[构造新 Node 写入 username id password isNull false expire]
    M --> N[push_back 到链表尾部]
    N --> O[在 index 记录 username 对应迭代器]
    O --> P[返回]
```

------

### 4. putNull 写入空值缓存

```mermaid
graph TD
    A[调用 LocalUserByName putNull] --> B[加锁]
    B --> C[在 index 中查找 username]

    C --> D{是否已存在}
    D -- 是 --> E[将 node.isNull 置为 true]
    E --> F[更新过期时间为 now 加 ttlSeconds]
    F --> G[LRU 更新 将节点移动到链表尾部]
    G --> H[返回]

    D -- 否 --> I{index 大小是否达到 capacity}
    I -- 是 --> J[淘汰 cache 链表头部最久未用节点]
    J --> K[从 index 删除最旧用户名]
    K --> L[cache 弹出头部节点]

    I --> M[构造新 Node id 为 0 password 置空 isNull true 设置过期时间]
    M --> N[push_back 到链表尾部]
    N --> O[在 index 记录 username 对应迭代器]
    O --> P[返回]
```

------

### 5. isNull 判断是否被缓存为空值

```mermaid
graph TD
    A[调用 LocalUserByName isNull] --> B[加锁]
    B --> C[在 index 中查找 username]

    C --> D{是否找到}
    D -- 否 --> E[返回 false]

    D -- 是 --> F[获取 Node 和当前时间 now]
    F --> G{now 是否大于等于 node.expire}

    G -- 是 --> H[从 cache 和 index 删除该节点]
    H --> I[返回 false 视为未命中]

    G -- 否 --> J[返回 node.isNull]
```

------

## 四、LocalUserCacheByPhone 本地手机号缓存

```mermaid
graph TD
    A[LocalUserCacheByPhone 本地手机号缓存] --> B[LRU 链表 list Node]
    A --> C[哈希索引 unordered_map phone 到迭代器]
    A --> D[每个 Node 包含 phone id username isNull expire]
    A --> E[get 根据手机号读取 做 TTL 检查和 LRU 更新]
    A --> F[put 写入正常用户数据]
    A --> G[putNull 写入空值缓存]
    A --> H[erase 根据手机号删除缓存]
```

------

## 五、SimpleQpsLimiter 限流器

### allow 调用流程

```mermaid
graph TD
    A[调用 SimpleQpsLimiter allow] --> B[获取当前时间 steady_clock now]
    B --> C[转换为秒级时间戳 sec]
    C --> D[加锁]

    D --> E{sec 是否等于 lastSec}
    E -- 否 --> F[更新 lastSec 为 sec 并将 count 重置为 0]
    F --> G[继续检查计数]

    E -- 是 --> G[使用当前计数 count]

    G --> H{count 是否小于 limit}
    H -- 是 --> I[count 自增 1]
    I --> J[返回 true 表示允许通过]

    H -- 否 --> K[返回 false 表示本秒已达到上限]
```

------

## 六、全局实例关系图

```mermaid
graph TD
    A[登录和鉴权相关业务] --> B[g_loginLimiter]
    A --> C[g_localUserByName]
    A --> D[g_localUserCacheByPhone]

    B --> E[限制每秒通过的请求数量 保护 Redis 和数据库]
    C --> F[按用户名做本地 LRU 和 TTL 缓存 支持空值缓存]
    D --> G[按手机号做本地 LRU 和 TTL 缓存 支持空值缓存]
```

