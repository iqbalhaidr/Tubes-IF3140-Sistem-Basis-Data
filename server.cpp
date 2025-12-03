#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <atomic>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <map>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "query_processor.h"
#include "types.h"

// Constants
const int PORT = 8080;
const int MAX_CLIENTS = 100;
const int BUFFER_SIZE = 4096;

// Global mutex for thread-safe operations
std::mutex server_mutex;
std::atomic<int> client_count{0};

// Forward declaration
std::string format_rows(const mdbms::Rows<mdbms::Row>& rows);

// Helper function to format ExecutionResult to string
std::string format_execution_result(const mdbms::ExecutionResult& result) {
    std::ostringstream oss;
    
    // Status message
    if (!result.message.empty()) {
        if (result.success) {
            oss << "[SUCCESS] " << result.message << "\n";
        } else {
            oss << "[ERROR] " << result.message << "\n";
        }
    }
    
    // Transaction ID
    if (result.transaction_id != -1) {
        oss << "Transaction ID: " << result.transaction_id << "\n";
    }
    
    // Display data if available (SELECT queries)
    if (result.data.rows_count > 0) {
        oss << "\n";
        oss << format_rows(result.data);
        oss << "\nTotal rows: " << result.data.rows_count << "\n";
    } else if (result.affected_rows > 0) {
        // For INSERT, UPDATE, DELETE
        oss << "Affected rows: " << result.affected_rows << "\n";
    }
    
    return oss.str();
}

// Helper function to format Rows<Row> to string (table format)
std::string format_rows(const mdbms::Rows<mdbms::Row>& rows) {
    if (rows.data.empty()) {
        return "No rows found.\n";
    }
    
    // Collect all column names
    std::vector<std::string> all_columns;
    if (!rows.column_names.empty()) {
        all_columns = rows.column_names;
    } else {
        std::map<std::string, bool> column_seen;
        for (const auto& row : rows.data) {
            for (const auto& pair : row.columns) {
                if (!column_seen[pair.first]) {
                    all_columns.push_back(pair.first);
                    column_seen[pair.first] = true;
                }
            }
        }
    }
    
    if (all_columns.empty()) {
        return "No columns to display.\n";
    }
    
    // Calculate column widths
    std::map<std::string, int> column_widths;
    for (const auto& col : all_columns) {
        column_widths[col] = col.length();
    }
    
    for (const auto& row : rows.data) {
        for (const auto& col : all_columns) {
            if (row.columns.count(col)) {
                std::string value_str;
                const auto& value = row.columns.at(col);
                
                if (value.type() == typeid(int)) {
                    value_str = std::to_string(std::any_cast<int>(value));
                } else if (value.type() == typeid(float)) {
                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(2) << std::any_cast<float>(value);
                    value_str = oss.str();
                } else if (value.type() == typeid(std::string)) {
                    value_str = std::any_cast<std::string>(value);
                } else {
                    value_str = "[unknown]";
                }
                
                if (value_str.length() > static_cast<size_t>(column_widths[col])) {
                    column_widths[col] = value_str.length();
                }
            }
        }
    }
    
    std::ostringstream oss;
    
    // Calculate total width for separator
    int total_width = 1;
    for (const auto& col : all_columns) {
        total_width += column_widths[col] + 3;
    }
    
    // Print separator
    oss << "+";
    for (int i = 0; i < total_width - 2; i++) {
        oss << "-";
    }
    oss << "+\n";
    
    // Print header
    oss << "|";
    for (const auto& col : all_columns) {
        oss << " " << std::setw(column_widths[col]) << std::left << col << " |";
    }
    oss << "\n";
    
    // Print separator
    oss << "+";
    for (int i = 0; i < total_width - 2; i++) {
        oss << "-";
    }
    oss << "+\n";
    
    // Print rows
    for (const auto& row : rows.data) {
        oss << "|";
        for (const auto& col : all_columns) {
            std::string value_str = "NULL";
            if (row.columns.count(col)) {
                const auto& value = row.columns.at(col);
                
                if (value.type() == typeid(int)) {
                    value_str = std::to_string(std::any_cast<int>(value));
                } else if (value.type() == typeid(float)) {
                    std::ostringstream val_oss;
                    val_oss << std::fixed << std::setprecision(2) << std::any_cast<float>(value);
                    value_str = val_oss.str();
                } else if (value.type() == typeid(std::string)) {
                    value_str = std::any_cast<std::string>(value);
                }
            }
            oss << " " << std::setw(column_widths[col]) << std::left << value_str << " |";
        }
        oss << "\n";
    }
    
    // Print separator
    oss << "+";
    for (int i = 0; i < total_width - 2; i++) {
        oss << "-";
    }
    oss << "+\n";
    
    return oss.str();
}

// Function to handle a single client connection
void handle_client(int client_socket, int client_id) {
    char buffer[BUFFER_SIZE];
    std::string client_info = "Client #" + std::to_string(client_id);
    
    // Create a QueryProcessor instance for this client
    // Each client gets its own QueryProcessor to avoid transaction conflicts
    mdbms::qp::QueryProcessor query_processor;
    
    {
        std::lock_guard<std::mutex> lock(server_mutex);
        std::cout << "[SERVER] " << client_info << " connected. Total clients: " 
                  << client_count.load() << std::endl;
    }
    
    // Send welcome message
    std::string welcome = "Connected to server. Send your queries (type 'exit' to disconnect).\n";
    send(client_socket, welcome.c_str(), welcome.length(), 0);
    
    while (true) {
        // Clear buffer
        memset(buffer, 0, BUFFER_SIZE);
        
        // Receive query from client
        ssize_t bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            // Client disconnected or error occurred
            if (bytes_received == 0) {
                std::lock_guard<std::mutex> lock(server_mutex);
                std::cout << "[SERVER] " << client_info << " disconnected gracefully." << std::endl;
            } else {
                std::lock_guard<std::mutex> lock(server_mutex);
                std::cerr << "[SERVER] Error receiving data from " << client_info << std::endl;
            }
            break;
        }
        
        // Null-terminate the received string
        buffer[bytes_received] = '\0';
        std::string query(buffer);
        
        // Remove trailing newline if present
        if (!query.empty() && query.back() == '\n') {
            query.pop_back();
        }
        if (!query.empty() && query.back() == '\r') {
            query.pop_back();
        }
        
        {
            std::lock_guard<std::mutex> lock(server_mutex);
            std::cout << "[SERVER] " << client_info << " sent query: " << query << std::endl;
        }
        
        // Check for exit command
        if (query == "exit" || query == "EXIT" || query == "quit" || query == "QUIT") {
            std::string response = "Goodbye! Connection closing.\n";
            send(client_socket, response.c_str(), response.length(), 0);
            break;
        }
        
        // Skip empty queries
        if (query.empty()) {
            continue;
        }
        
        // Execute query using QueryProcessor
        std::string response;
        try {
            mdbms::ExecutionResult result = query_processor.execute_query(query);
            response = format_execution_result(result);
            
            {
                std::lock_guard<std::mutex> lock(server_mutex);
                if (result.success) {
                    std::cout << "[SERVER] " << client_info << " query executed successfully." << std::endl;
                } else {
                    std::cout << "[SERVER] " << client_info << " query failed: " << result.message << std::endl;
                }
            }
        } catch (const std::exception& e) {
            response = "[ERROR] " + std::string(e.what()) + "\n";
            {
                std::lock_guard<std::mutex> lock(server_mutex);
                std::cerr << "[SERVER] " << client_info << " exception: " << e.what() << std::endl;
            }
        }
        
        // Add newline at the end if not present
        if (!response.empty() && response.back() != '\n') {
            response += "\n";
        }
        
        // Send response back to client
        ssize_t bytes_sent = send(client_socket, response.c_str(), response.length(), 0);
        if (bytes_sent < 0) {
            std::lock_guard<std::mutex> lock(server_mutex);
            std::cerr << "[SERVER] Error sending response to " << client_info << std::endl;
            break;
        }
    }
    
    // Clean up: close client socket
    close(client_socket);
    client_count--;
    
    {
        std::lock_guard<std::mutex> lock(server_mutex);
        std::cout << "[SERVER] " << client_info << " disconnected. Remaining clients: " 
                  << client_count.load() << std::endl;
    }
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_id_counter = 0;
    
    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        std::cerr << "[SERVER] Error creating socket." << std::endl;
        return 1;
    }
    
    // Set socket options to allow reuse of address
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "[SERVER] Error setting socket options." << std::endl;
        close(server_socket);
        return 1;
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all interfaces
    server_addr.sin_port = htons(PORT);
    
    // Bind socket to address
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[SERVER] Error binding socket to port " << PORT << std::endl;
        close(server_socket);
        return 1;
    }
    
    // Listen for connections
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        std::cerr << "[SERVER] Error listening on socket." << std::endl;
        close(server_socket);
        return 1;
    }
    
    std::cout << "[SERVER] Server started. Listening on localhost:" << PORT << std::endl;
    std::cout << "[SERVER] Waiting for client connections..." << std::endl;
    
    // Vector to store client threads
    std::vector<std::thread> client_threads;
    
    // Main server loop: accept connections
    while (true) {
        // Accept new client connection
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        
        if (client_socket < 0) {
            std::cerr << "[SERVER] Error accepting client connection." << std::endl;
            continue;
        }
        
        // Check if we've reached max clients
        if (client_count.load() >= MAX_CLIENTS) {
            std::string reject_msg = "Server is at maximum capacity. Please try again later.\n";
            send(client_socket, reject_msg.c_str(), reject_msg.length(), 0);
            close(client_socket);
            continue;
        }
        
        // Increment client count
        client_count++;
        int client_id = ++client_id_counter;
        
        // Spawn a new thread for this client
        std::thread client_thread(handle_client, client_socket, client_id);
        client_thread.detach();  // Detach thread so it runs independently
    }
    
    // This code should never be reached in normal operation
    // But if we need to shut down gracefully, we would:
    // 1. Set a flag to stop accepting new connections
    // 2. Wait for all client threads to finish
    // 3. Close the server socket
    
    close(server_socket);
    return 0;
}

