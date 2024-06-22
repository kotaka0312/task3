#include <stdio.h>          // 标准输入输出库
#include <stdlib.h>         // 标准库，包括内存分配、进程控制等
#include <string.h>         // 字符串处理库
#include <unistd.h>         // UNIX 标准库，包含各种系统调用
#include <arpa/inet.h>      // 包含网络编程所需的函数
#include <fcntl.h>          // 文件控制库，用于设置非阻塞模式
#include <sys/select.h>     // 多路复用I/O库

#define BUFFER_SIZE 1024    // 定义缓冲区大小
#define MAX_CLIENTS 10      // 最大客户端连接数

// 错误处理函数，打印错误消息并退出程序
void error(const char *msg) {
    perror(msg);
    exit(1);
}

// 反转字符串
void reverse_string(char *str, int length) {
    int i, j;
    char temp;
    for (i = 0, j = length - 1; i < j; i++, j--) {
        temp = str[i];
        str[i] = str[j];
        str[j] = temp;
    }
}

// 处理客户端请求
void handle_client(int client_sock) {
    uint32_t block_count_net;
    ssize_t recv_len;

    // 接收块数
    recv_len = recv(client_sock, &block_count_net, sizeof(block_count_net), 0);
    if (recv_len <= 0) {
        error("接收块数失败");
    }
    uint32_t block_count = ntohl(block_count_net); // 将块数转换为主机字节序

    printf("接收到初始化数据包，块数：%d\n", block_count);

    // 发送同意消息
    if (send(client_sock, "同意", 6, 0) <= 0) {
        error("发送同意消息失败");
    }

    for (uint32_t i = 0; i < block_count; i++) {
        uint32_t data_length_net;

        // 接收数据长度
        recv_len = recv(client_sock, &data_length_net, sizeof(data_length_net), 0);
        if (recv_len <= 0) {
            error("接收数据长度失败");
        }
        uint32_t data_length = ntohl(data_length_net); // 将数据长度转换为主机字节序

        printf("接收到块 %d，大小：%d\n", i + 1, data_length);

        // 检查反转数据长度是否超过缓冲区大小
        if (data_length > BUFFER_SIZE) {
            error("反转数据长度超过缓冲区大小");
        }

        char *data = malloc(data_length + 1); // 为数据分配内存
        if (data == NULL) {
            error("内存分配失败");
        }

        // 接收数据
        recv_len = recv(client_sock, data, data_length, 0);
        if (recv_len <= 0) {
            error("接收数据失败");
        }
        data[data_length] = '\0'; // 确保字符串末尾的null字符存在

        // 反转字符串
        reverse_string(data, data_length);

        // 将反转后的数据长度转换为网络字节序
        uint32_t reversed_length_net = htonl(data_length);
        if (send(client_sock, &reversed_length_net, sizeof(reversed_length_net), 0) <= 0) {
            error("发送反转数据长度失败");
        }
        if (send(client_sock, data, data_length, 0) <= 0) {
            error("发送反转数据失败");
        }

        free(data); // 释放内存
    }

    close(client_sock); // 关闭客户端socket
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "用法: %s <端口>\n", argv[0]);
        exit(1); // 检查命令行参数，如果数量不对则退出
    }

    int server_port = atoi(argv[1]); // 获取服务器端口
    int server_sock, client_sock; // 套接字文件描述符
    struct sockaddr_in server_addr, client_addr; // 服务器和客户端地址结构
    socklen_t client_len = sizeof(client_addr); // 客户端地址长度
    fd_set read_fds, master_fds; // 文件描述符集合
    int fdmax;

    // 创建socket
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        error("创建套接字失败");
    }

    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        error("设置套接字选项失败");
    }

    server_addr.sin_family = AF_INET; // 设置地址族为IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // 接受任何地址的连接
    server_addr.sin_port = htons(server_port); // 设置端口号

    // 绑定socket
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        error("绑定套接字失败");
    }

    // 监听socket
    if (listen(server_sock, 5) < 0) {
        error("监听失败");
    }

    printf("服务器正在监听端口 %d\n", server_port);

    // 设置 server_sock 为非阻塞模式
    if (fcntl(server_sock, F_SETFL, O_NONBLOCK) < 0) {
        error("设置非阻塞模式失败");
    }

    FD_ZERO(&master_fds);
    FD_SET(server_sock, &master_fds);
    fdmax = server_sock;

    while (1) {
        read_fds = master_fds;

        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
            error("select 失败");
        }

        for (int i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) {
                if (i == server_sock) {
                    client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
                    if (client_sock == -1) {
                        perror("接受客户端连接失败");
                    } else {
                        printf("接受到来自 %s:%d 的连接\n",
                               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                        FD_SET(client_sock, &master_fds);
                        if (client_sock > fdmax) {
                            fdmax = client_sock;
                        }
                    }
                } else {
                    handle_client(i);
                    FD_CLR(i, &master_fds);
                }
            }
        }
    }

    close(server_sock); // 关闭服务器socket
    return 0; // 退出程序
}
