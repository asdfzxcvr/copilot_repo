#include <iostream>
#include <cstdlib>
#include <string>
#include <thread>
#include <atomic>
#include <sstream>
#include <cstring>
#include <map>
#include <mutex>
#include <condition_variable>
#include <netinet/in.h>
#include <unistd.h>

// Configuration from environment variables
std::string getEnv(const char* var, const char* def = "") {
    const char* val = std::getenv(var);
    return val ? std::string(val) : std::string(def);
}

const std::string DEVICE_IP   = getEnv("DEVICE_IP",   "127.0.0.1");
const std::string HTTP_HOST   = getEnv("HTTP_HOST",   "0.0.0.0");
const int         HTTP_PORT   = std::stoi(getEnv("HTTP_PORT", "8080"));

// Camera state
enum class CameraStatus { STOPPED, RUNNING, ERROR };
std::atomic<CameraStatus> camera_state(CameraStatus::STOPPED);
std::mutex status_mutex;
std::string last_error;
std::condition_variable cv;

// Utility: JSON response
std::string jsonResponse(const std::map<std::string, std::string>& fields) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& kv : fields) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << kv.first << "\":\"" << kv.second << "\"";
    }
    oss << "}";
    return oss.str();
}

// Camera startup simulation
bool startCamera() {
    std::lock_guard<std::mutex> lg(status_mutex);
    // Simulate initialization. Real device control would go here.
    camera_state = CameraStatus::RUNNING;
    last_error.clear();
    return true;
}

std::string getCameraStatusJson() {
    std::lock_guard<std::mutex> lg(status_mutex);
    std::string status_str, error_str;
    switch (camera_state.load()) {
        case CameraStatus::RUNNING:
            status_str = "running";
            break;
        case CameraStatus::STOPPED:
            status_str = "stopped";
            break;
        case CameraStatus::ERROR:
            status_str = "error";
            break;
    }
    if (!last_error.empty()) error_str = last_error;
    return jsonResponse({
        {"status", status_str},
        {"error", error_str}
    });
}

// HTTP server helpers
int createListenSocket(const std::string& host, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = (host == "0.0.0.0") ? INADDR_ANY : inet_addr(host.c_str());
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }
    if (listen(sockfd, 8) < 0) {
        close(sockfd);
        return -1;
    }
    return sockfd;
}

void sendHttp(int client, const std::string& code, const std::string& contentType, const std::string& body) {
    std::ostringstream resp;
    resp << "HTTP/1.1 " << code << "\r\n";
    resp << "Content-Type: " << contentType << "\r\n";
    resp << "Content-Length: " << body.size() << "\r\n";
    resp << "Connection: close\r\n";
    resp << "\r\n";
    resp << body;
    std::string respStr = resp.str();
    send(client, respStr.c_str(), respStr.size(), 0);
}

void handleRequest(int client) {
    char buf[4096];
    int n = recv(client, buf, sizeof(buf)-1, 0);
    if (n <= 0) { close(client); return; }
    buf[n] = 0;
    std::istringstream req(buf);
    std::string line, method, path;
    if (!std::getline(req, line)) { close(client); return; }
    std::istringstream l(line);
    l >> method >> path;

    // Route: /camera/start (POST)
    if (path == "/camera/start" && method == "POST") {
        bool ok = startCamera();
        std::string body = jsonResponse({
            {"success", ok ? "true" : "false"},
            {"message", ok ? "Camera started" : "Failed to start camera"}
        });
        sendHttp(client, "200 OK", "application/json", body);
        close(client);
        return;
    }

    // Route: /camera/status (GET)
    if (path == "/camera/status" && method == "GET") {
        std::string body = getCameraStatusJson();
        sendHttp(client, "200 OK", "application/json", body);
        close(client);
        return;
    }

    // Not found
    sendHttp(client, "404 Not Found", "application/json", "{\"error\":\"Not Found\"}");
    close(client);
}

void serverMain() {
    int sockfd = createListenSocket(HTTP_HOST, HTTP_PORT);
    if (sockfd < 0) {
        std::cerr << "Failed to bind HTTP server on " << HTTP_HOST << ":" << HTTP_PORT << std::endl;
        exit(1);
    }
    while (true) {
        sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int client = accept(sockfd, (struct sockaddr*)&cli_addr, &cli_len);
        if (client < 0) continue;
        std::thread(handleRequest, client).detach();
    }
}

int main() {
    std::cout << "UGREEN Camera HTTP Driver starting on " << HTTP_HOST << ":" << HTTP_PORT << std::endl;
    camera_state = CameraStatus::STOPPED;
    serverMain();
    return 0;
}