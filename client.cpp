#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Constants
const int PORT = 8080;
const char* SERVER_IP = "127.0.0.1";
const int BUFFER_SIZE = 4096;

int main() {
    int client_socket;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    
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
    memset(buffer, 0, BUFFER_SIZE);
    ssize_t bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        std::cout << buffer;
    }
    
    // Main client loop: send queries and receive results
    std::string query;
    while (true) {
        // Get user input
        std::cout << "> ";
        std::getline(std::cin, query);
        
        // Check for exit command
        if (query == "exit" || query == "EXIT" || query == "quit" || query == "QUIT") {
            // Send exit command to server
            query += "\n";
            send(client_socket, query.c_str(), query.length(), 0);
            
            // Receive goodbye message
            memset(buffer, 0, BUFFER_SIZE);
            bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                std::cout << buffer;
            }
            break;
        }
        
        // Check for empty query
        if (query.empty()) {
            continue;
        }
        
        // Send query to server (append newline)
        query += "\n";
        ssize_t bytes_sent = send(client_socket, query.c_str(), query.length(), 0);
        if (bytes_sent < 0) {
            std::cerr << "[CLIENT] Error sending query to server." << std::endl;
            break;
        }
        
        // Receive response from server
        memset(buffer, 0, BUFFER_SIZE);
        bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                std::cout << "[CLIENT] Server closed the connection." << std::endl;
            } else {
                std::cerr << "[CLIENT] Error receiving response from server." << std::endl;
            }
            break;
        }
        
        // Display response
        buffer[bytes_received] = '\0';
        std::cout << buffer;
    }
    
    // Clean up: close socket
    close(client_socket);
    std::cout << "[CLIENT] Disconnected from server." << std::endl;
    
    return 0;
}

