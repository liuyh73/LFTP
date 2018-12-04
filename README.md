# LFTP

## Introduction

- Use UDP as the transport layer protocol.
- Realize 100% reliability as TCP.
- Implement flow control function similar as TCP.
- Implement congestion control function similar as TCP.
- Support multiple clients as the same time.

## Usage

- Server

  ```bash
  # 在server文件夹下编译源文件
  $ g++ main.cpp server.cpp ..\role\receiver.cpp ..\role\sender.cpp -o server.exe -std=c++11 -lwsock32
  ```

- Client

  ```bash
  # 在client文件夹下编译源文件
  $ g++ main.cpp ..\role\receiver.cpp ..\role\sender.cpp -o lftp.exe -std=c++11 -lwsock32
  ```

- **lget**和**lsend**命令

  ```bash
  # 文件上传和下载所在路径默认为lftp.exe、server.exe所在路径
  # 启动ftp服务器
  $ ./server.exe
  
  # lget
  $ ./lftp.exe lget 127.0.0.1 music.mp3
  # lsend
  $ ./lftp.exe lsend 127.0.0.1 music.mp3
  ```

## Notes

- Please not upload the existed file，there may be some error.
- I don't check the commands, please use the above-mentioned commands.
- [Github](https://github.com/liuyh73/lftp)