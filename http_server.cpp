#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <thread>

using namespace std;

// 这个http服务器会将请求的method+url直接作为body返回
class SimpleHttpServer
{
public:
    SimpleHttpServer(const string& ip, uint16_t port) : ip_(ip), port_(port), is_running_(false), ec_(0), delay_time_(0)
    {
    }

    std::string generateHTTPResponse(const std::string& requestURL) const;
    std::string extractURLFromRequest(const std::string& request) const;
    void handle_client(int client_socket) const;
    void stop_server() { is_running_ = false; }
    bool is_running() const { return is_running_; }
    int get_error_code() const { return ec_; }
    void set_delay_ms(int time) { delay_time_ = time; }
    void start();

private:
    string ip_;
    uint16_t port_;
    std::atomic_bool is_running_;
    int ec_;
    std::atomic_int delay_time_;
    static constexpr int BUFFER_SIZE = 2048;
};


void SimpleHttpServer::start()
{
    int server_socket;
    struct sockaddr_in server_address, client_address;
    socklen_t client_address_size = sizeof(client_address);

    // 创建套接字
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        std::cerr << "Failed to create socket." << std::endl;
        ec_ = 1;
        return;
    }

    // 启用端口重用
    int reuse = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1)
    {
        std::cerr << "Failed to set socket option." << std::endl;
        ec_ = 1;
        return;
    }

    // 设置服务器地址
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = inet_addr(ip_.c_str());
    server_address.sin_port = htons(port_);

    // 绑定套接字到指定地址和端口
    if (bind(server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) == -1)
    {
        std::cerr << "Failed to bind socket." << std::endl;
        ec_ = 1;
        return;
    }

    // 设置套接字为非阻塞模式
    int flags = fcntl(server_socket, F_GETFL, 0);
    if (fcntl(server_socket, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        std::cerr << "Failed to set socket to non-blocking mode." << std::endl;
        return;
    }

    // 监听连接请求
    if (listen(server_socket, 500) == -1)
    {
        std::cerr << "Failed to listen." << std::endl;
        ec_ = 1;
        return;
    }

    std::vector<std::thread> threads;
    is_running_ = true;
    while (is_running_)
    {
        // 接受连接请求
        int client_socket = accept(server_socket, (struct sockaddr*)&client_address, &client_address_size);
        if (client_socket == -1)
        {
            // 如果没有连接请求，继续下一次循环
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                std::this_thread::sleep_for(5ns);
                continue;
            }
            std::cerr << "Failed to accept connection." << std::endl;
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_address.sin_addr), client_ip, INET_ADDRSTRLEN);

        // 创建新线程处理连接
        std::thread client_thread(&SimpleHttpServer::handle_client, this, client_socket);
        threads.push_back(std::move(client_thread));
    }

    // 等待所有线程结束
    for (auto& thread : threads)
        thread.join();
    close(server_socket);
    ec_ = 1;
    return;
}

void SimpleHttpServer::handle_client(int client_socket) const
{
    char buffer[BUFFER_SIZE];
    std::string req, rsp;

    int bytes_received;
    while ((bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0)
    {
        req += std::string(buffer, bytes_received);

        // 检查请求是否完整
        if (req.find("\r\n\r\n") != std::string::npos)
        {
            // 完整的请求接收完毕，回复url
            rsp = generateHTTPResponse(extractURLFromRequest(req));
            break;
        }
    }

    if (delay_time_ != 0)
        this_thread::sleep_for(std::chrono::milliseconds(delay_time_));
    send(client_socket, rsp.c_str(), rsp.length(), 0);
    close(client_socket);
}

std::string SimpleHttpServer::extractURLFromRequest(const std::string& request) const
{
    std::istringstream iss(request);
    std::string firstLine;
    std::getline(iss, firstLine);  // 获取请求的第一行

    std::istringstream lineStream(firstLine);
    std::string method, url, protocol;
    lineStream >> method >> url >> protocol;  // 解析第一行，提取方法、URL和协议版本

    return method + url;
}

std::string SimpleHttpServer::generateHTTPResponse(const std::string& requestURL) const
{
    std::string response;

    // 构建HTTP状态行
    response += "HTTP/1.1 200 OK\r\n";

    // 构建HTTP头部
    response += "Content-Type: text/plain\r\n";
    response += "Content-Length: " + std::to_string(requestURL.length()) + "\r\n";
    response += "Connection: close\r\n";

    // 构建HTTP空行
    response += "\r\n";

    // 添加内容
    response += requestURL;

    return response;
}
