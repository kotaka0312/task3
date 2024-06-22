#include <stdio.h>          // 标准输入输出库
#include <stdlib.h>         // 标准库，包括内存分配、进程控制等
#include <string.h>         // 字符串处理库
#include <unistd.h>         // UNIX 标准库，包含各种系统调用
#include <arpa/inet.h>      // 包含网络编程所需的函数
#include <time.h>           // 时间库，用于随机数生成

#define BUFFER_SIZE 4096    // 增大缓冲区大小

// 错误处理函数，打印错误消息并退出程序
void error(const char *msg) {
    perror(msg);
    exit(1);
}

// 读取文件内容并将其存储在 content 中，length 返回文件长度
void read_file(const char *filename, char **content, long *length) {
    FILE *file = fopen(filename, "r");   // 打开文件
    if (file == NULL) {
        error("Failed to open file");    // 打开文件失败则报错
    }

    fseek(file, 0, SEEK_END);            // 将文件指针移动到文件末尾
    *length = ftell(file);               // 获取文件长度
    fseek(file, 0, SEEK_SET);            // 将文件指针移回文件开头

    *content = malloc(*length);          // 为文件内容分配内存
    if (*content == NULL) {
        error("Memory allocation failed"); // 内存分配失败则报错
    }

    fread(*content, 1, *length, file);   // 读取文件内容
    fclose(file);                        // 关闭文件
}

// 发送数据包
void send_packet(int sockfd, const void *packet, size_t length) {
    if (send(sockfd, packet, length, 0) < 0) {
        error("Failed to send packet");  // 发送失败则报错
    }
}

// 接收数据包
ssize_t receive_packet(int sockfd, void *buffer, size_t length) {
    ssize_t bytes_received = recv(sockfd, buffer, length, 0); // 接收数据
    if (bytes_received < 0) {
        error("Failed to receive packet"); // 接收失败则报错
    }
    return bytes_received;  // 返回接收的数据字节数
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "用法: %s <服务器IP> <服务器端口> <Lmin> <Lmax>\n", argv[0]);
        exit(1);             // 检查命令行参数，如果数量不对则退出
    }

    const char *server_ip = argv[1];      // 获取服务器IP
    int server_port = atoi(argv[2]);      // 获取服务器端口
    int Lmin = atoi(argv[3]);             // 获取最小块大小
    int Lmax = atoi(argv[4]);             // 获取最大块大小

    int sockfd;                           // 套接字文件描述符
    struct sockaddr_in server_addr;       // 服务器地址结构

    // 创建socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        error("创建套接字失败");  // 创建socket失败则报错
    }

    server_addr.sin_family = AF_INET;     // 设置地址族为IPv4
    server_addr.sin_port = htons(server_port); // 设置端口号
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        error("无效地址");         // 将IP地址从文本转换为二进制格式
    }

    // 连接服务器
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        error("连接失败");       // 连接失败则报错
    }

    char *file_content;                   // 文件内容指针
    long file_length;                     // 文件长度
    read_file("ascii_file.txt", &file_content, &file_length); // 读取文件内容

    srand(time(NULL));                    // 初始化随机数生成器
    int total_blocks = 0;                 // 总块数
    long remaining = file_length;         // 剩余文件长度
    while (remaining > 0) {
        // 确定块大小，最后一块可能小于Lmin
        int block_size = (remaining < Lmin) ? remaining : (rand() % (Lmax - Lmin + 1) + Lmin);
        remaining -= block_size;          // 减少剩余长度
        total_blocks++;                   // 增加块数
    }

    // 发送初始化报文
    uint32_t block_count = htonl(total_blocks); // 将块数转换为网络字节序
    printf("发送初始化数据包，块数：%d\n", total_blocks);
    send_packet(sockfd, &block_count, sizeof(block_count)); // 发送块数

    char buffer[BUFFER_SIZE];             // 缓冲区
    ssize_t bytes_received = receive_packet(sockfd, buffer, BUFFER_SIZE); // 接收服务器响应
    buffer[bytes_received] = '\0';        // 添加字符串结束符
    printf("接收到服务器响应：%s\n", buffer);

    if (strcmp(buffer, "同意") != 0) {   // 检查服务器响应
        fprintf(stderr, "服务器不同意初始化\n");
        close(sockfd);                    // 关闭socket
        free(file_content);               // 释放文件内容内存
        exit(1);                          // 退出程序
    }

    remaining = file_length;              // 重新设置剩余长度
    int block_num = 0;                    // 块编号
    while (remaining > 0) {
        // 确定块大小，最后一块可能小于Lmin
        int block_size = (remaining < Lmin) ? remaining : (rand() % (Lmax - Lmin + 1) + Lmin);
        char *block_data = file_content + (file_length - remaining); // 块数据指针
        remaining -= block_size;          // 减少剩余长度

        uint32_t data_length = htonl(block_size); // 将数据长度转换为网络字节序
        printf("发送第%d块，大小：%d\n", block_num + 1, block_size);
        send_packet(sockfd, &data_length, sizeof(data_length)); // 发送数据长度
        send_packet(sockfd, block_data, block_size); // 发送数据

        size_t total_received = 0;        // 接收总字节数
        while (total_received < 4) {      // 确保接收完整的长度信息
            bytes_received = receive_packet(sockfd, buffer + total_received, 4 - total_received);
            total_received += bytes_received;
        }

        uint32_t reversed_length = ntohl(*(uint32_t *)buffer); // 接收反转后的数据长度
        total_received = 0;               // 重置接收总字节数

        while (total_received < reversed_length) { // 确保接收完整的反转数据
            bytes_received = receive_packet(sockfd, buffer + 4 + total_received, reversed_length - total_received);
            total_received += bytes_received;
        }

        buffer[reversed_length + 4] = '\0'; // 确保字符串末尾的null字符存在
        printf("第%d块: %s\n", ++block_num, buffer + 4); // 打印反转后的数据
    }

    close(sockfd);                        // 关闭socket
    free(file_content);                   // 释放文件内容内存
    return 0;                             // 退出程序
}
