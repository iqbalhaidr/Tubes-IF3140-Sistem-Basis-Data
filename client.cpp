#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdint>

// Constants
const int PORT = 8080;
const char* SERVER_IP = "127.0.0.1";

bool send_all(int socket_fd, const char* data, size_t length) {
    size_t total_sent = 0;
    while (total_sent < length) {
        ssize_t bytes = send(socket_fd, data + total_sent, length - total_sent, 0);
        if (bytes <= 0) {
            return false;
        }
        total_sent += static_cast<size_t>(bytes);
    }
    return true;
}

bool recv_all(int socket_fd, char* buffer, size_t length) {
    size_t total_received = 0;
    while (total_received < length) {
        ssize_t bytes = recv(socket_fd, buffer + total_received, length - total_received, 0);
        if (bytes <= 0) {
            return false;
        }
        total_received += static_cast<size_t>(bytes);
    }
    return true;
}

bool receive_message(int socket_fd, std::string& out) {
    uint32_t net_length = 0;
    if (!recv_all(socket_fd, reinterpret_cast<char*>(&net_length), sizeof(net_length))) {
        return false;
    }
    uint32_t length = ntohl(net_length);
    std::string buffer(length, '\0');
    if (length > 0 && !recv_all(socket_fd, buffer.data(), length)) {
        return false;
    }
    out.swap(buffer);
    return true;
}

int main() {
    int client_socket;
    struct sockaddr_in server_addr;
    
    // Create socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        std::cerr << "[CLIENT] Error creating socket." << std::endl;
        return 1;
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    
    // Convert IP address from string to binary form
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        std::cerr << "[CLIENT] Invalid address or address not supported." << std::endl;
        close(client_socket);
        return 1;
    }
    
    // Connect to server
    std::cout << "[CLIENT] Connecting to server at " << SERVER_IP << ":" << PORT << "..." << std::endl;
    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[CLIENT] Connection failed. Make sure the server is running." << std::endl;
        close(client_socket);
        return 1;
    }
    
    std::cout << "[CLIENT] Connected to server successfully!" << std::endl;
    std::cout << "[CLIENT] Type your queries (or 'exit' to disconnect):" << std::endl;
    std::cout << std::endl;
    
    // Receive welcome message from server
    std::string message;
    if (receive_message(client_socket, message)) {
        std::cout << message;
    } else {
        std::cerr << "[CLIENT] Failed to receive welcome message." << std::endl;
        close(client_socket);
        return 1;
    }
    
    // Main client loop: send queries and receive results
    std::string query;
    while (true) {
        // Get user input
        std::cout << "> ";
        if (!std::getline(std::cin, query)) {
            std::cout << "\n[CLIENT] Input stream closed. Exiting." << std::endl;
            break;
        }
        
        // Check for exit command
        if (query == "exit" || query == "EXIT" || query == "quit" || query == "QUIT") {
            // Send exit command to server
            query += "\n";
            if (!send_all(client_socket, query.c_str(), query.length())) {
                std::cerr << "[CLIENT] Failed to send exit command." << std::endl;
                break;
            }
            
            // Receive goodbye message
            std::string goodbye;
            if (receive_message(client_socket, goodbye)) {
                std::cout << goodbye;
            }
            break;
        }
        
        // Check for empty query
        if (query.empty()) {
            continue;
        }
        
        // Send query to server (append newline)
        query += "\n";
        if (!send_all(client_socket, query.c_str(), query.length())) {
            std::cerr << "[CLIENT] Error sending query to server." << std::endl;
            break;
        }
        
        // Receive response from server
        std::string response;
        if (!receive_message(client_socket, response)) {
            std::cout << "[CLIENT] Server closed the connection or an error occurred." << std::endl;
            break;
        }
        
        std::cout << response;
    }
    
    // Clean up: close socket
    close(client_socket);
    std::cout << "[CLIENT] Disconnected from server." << std::endl;
    
    return 0;
}

