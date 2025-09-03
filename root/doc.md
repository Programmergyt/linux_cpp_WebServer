好👌，我在上一个 **Linux C++ WebServer DEMO** 的基础上，给你扩展了几个典型的“文件请求”接口（图片、视频、静态文件下载等），这样你在做性能测试时可以更全面覆盖 **静态资源请求 + 动态接口请求**。我会给你完整的网站示例，包括文件结构、HTML 页面、接口文档、数据库定义和样例数据。

---

## 📂 文件结构

```
/web_demo
│── index.html                # 首页
│── login.html                # 登录页
│── register.html             # 注册页
│── upload.html               # 文件上传页（模拟）
│
├── assets/                   # 静态资源目录
│   ├── css/
│   │   └── style.css
│   ├── img/
│   │   ├── logo.png
│   │   └── sample.jpg
│   ├── video/
│   │   └── demo.mp4
│   └── files/
│       └── example.pdf
│
└── api_doc.md                # 接口文档（说明用）
```

---

## 🌐 HTML 页面示例

### 1. `index.html`

```html
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>WebServer Demo</title>
  <link rel="stylesheet" href="assets/css/style.css">
</head>
<body>
  <h1>欢迎来到 C++ WebServer Demo</h1>
  <p>这是一个测试性能用的网站示例。</p>

  <h2>功能入口</h2>
  <ul>
    <li><a href="/login.html">用户登录</a></li>
    <li><a href="/register.html">用户注册</a></li>
    <li><a href="/upload.html">上传文件（模拟）</a></li>
  </ul>

  <h2>静态资源</h2>
  <ul>
    <li><a href="/assets/img/logo.png">查看图片 (PNG)</a></li>
    <li><a href="/assets/img/sample.jpg">查看图片 (JPG)</a></li>
    <li><a href="/assets/video/demo.mp4">播放视频 (MP4)</a></li>
    <li><a href="/assets/files/example.pdf">下载文件 (PDF)</a></li>
  </ul>
</body>
</html>
```

### 2. `login.html`

```html
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>登录</title>
</head>
<body>
  <h2>登录</h2>
  <form method="POST" action="/api/login">
    用户名: <input type="text" name="username"><br>
    密码: <input type="password" name="password"><br>
    <button type="submit">登录</button>
  </form>
</body>
</html>
```

### 3. `register.html`

```html
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>注册</title>
</head>
<body>
  <h2>注册</h2>
  <form method="POST" action="/api/register">
    用户名: <input type="text" name="username"><br>
    密码: <input type="password" name="password"><br>
    邮箱: <input type="email" name="email"><br>
    <button type="submit">注册</button>
  </form>
</body>
</html>
```

### 4. `upload.html`

```html
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>上传文件</title>
</head>
<body>
  <h2>上传文件</h2>
  <form method="POST" action="/api/upload" enctype="multipart/form-data">
    选择文件: <input type="file" name="file"><br>
    <button type="submit">上传</button>
  </form>
</body>
</html>
```

---

## 📖 接口文档 (`api_doc.md`)

### 用户接口

| 接口              | 方法   | 请求参数                      | 响应示例                                             |
| --------------- | ---- | ------------------------- | ------------------------------------------------ |
| `/api/register` | POST | username, password, email | `{ "status": "ok", "msg": "注册成功" }`              |
| `/api/login`    | POST | username, password        | `{ "status": "ok", "msg": "登录成功" }`              |
| `/api/user/:id` | GET  | user id                   | `{ "id": 1, "username": "test", "email": "xx" }` |

### 文件接口

| 接口                          | 方法   | 描述               |
| --------------------------- | ---- | ---------------- |
| `/assets/img/logo.png`      | GET  | 返回图片 (静态资源)      |
| `/assets/video/demo.mp4`    | GET  | 返回视频流            |
| `/assets/files/example.pdf` | GET  | 返回 PDF 文件        |
| `/api/upload`               | POST | 接收上传文件（可模拟保存到磁盘） |

---

## 🗄️ 数据库定义

```sql
CREATE DATABASE web_demo CHARSET utf8mb4;

USE web_demo;

CREATE TABLE users (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    password VARCHAR(100) NOT NULL,
    email VARCHAR(100),
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 插入样例数据
INSERT INTO users (username, password, email) VALUES
('alice', '123456', 'alice@example.com'),
('bob', 'abcdef', 'bob@example.com');
```

---

## ✅ 使用说明

* **静态请求测试**：访问 `/assets/img/logo.png`、`/assets/video/demo.mp4`，模拟高并发文件传输。
* **动态请求测试**：`/api/register`、`/api/login`，测试数据库操作。
* **组合场景测试**：前端 HTML 页面 + API 请求 + 文件请求，综合压测。

---

要做 **性能压测 (QPS)**，你就可以用 **ab、wrk、hey** 之类的工具对 `/assets/video/demo.mp4`、`/api/login`、`/api/register` 发请求，看 **静态 vs 动态 vs 文件请求** 的表现差异。

要不要我帮你写一个 **wrk 压测命令示例**，分别压测 `/api/login` 和 `/assets/video/demo.mp4` 的性能？
