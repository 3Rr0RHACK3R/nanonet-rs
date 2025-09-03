#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <mswsock.h> // For AcceptEx

#define BUFFER_SIZE 4096
#define MAX_THREADS 64

// An enum to define the type of operation in an OVERLAPPED structure
typedef enum {
    IO_ACCEPT,
    IO_READ,
    IO_WRITE
} OperationType;

// A professional-grade callback function for Rust to hook into the C core.
typedef int (*NanoNetCallback)(void* client_handle, const char* data, int data_len);

// Struct to hold per-I/O data
typedef struct _PER_IO_DATA {
    WSAOVERLAPPED overlapped;
    WSABUF data_buf;
    char buffer[BUFFER_SIZE];
    OperationType operation_type;
} PER_IO_DATA, *LPPER_IO_DATA;

// Struct to hold per-handle data (a client socket)
typedef struct _PER_HANDLE_DATA {
    SOCKET socket;
} PER_HANDLE_DATA, *LPPER_HANDLE_DATA;

// Global variables for the server core
HANDLE g_completion_port = NULL;
SOCKET g_listen_socket = INVALID_SOCKET;
volatile int g_shutdown_flag = 0;
HANDLE g_worker_threads[MAX_THREADS];
DWORD g_num_threads = 0;
NanoNetCallback g_callback_func = NULL;

// Function pointer for AcceptEx
LPFN_ACCEPTEX lpfnAcceptEx = NULL;

// Function prototypes
void cleanup();
void post_accept();
void post_read(LPPER_HANDLE_DATA per_handle_data);
void post_write(LPPER_HANDLE_DATA per_handle_data, const char* data, int data_len);
void process_io(LPPER_HANDLE_DATA per_handle_data, LPPER_IO_DATA per_io_data, DWORD bytes_transferred);
DWORD WINAPI WorkerThread(LPVOID lpParam);

// Professional-grade API for the Rust side
extern int initialize_server(const char* addr, unsigned short port, NanoNetCallback callback_func);
extern void start_server();
extern void shutdown_server();

/**
 * Initializes the server, binds to an address, and starts worker threads.
 *
 * @param addr The address to bind to (e.g., "0.0.0.0" for all interfaces).
 * @param port The port to listen on.
 * @param callback_func A function pointer to the Rust callback for data processing.
 * @return 0 on success, non-zero on error.
 */
int initialize_server(const char* addr, unsigned short port, NanoNetCallback callback_func) {
    WSADATA wsaData;
    int iResult;

    // Check for a valid callback function
    if (callback_func == NULL) {
        return 1;
    }
    g_callback_func = callback_func;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        return 2;
    }

    // Create the I/O Completion Port
    g_completion_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (g_completion_port == NULL) {
        WSACleanup();
        return 3;
    }

    // Create the listening socket
    g_listen_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (g_listen_socket == INVALID_SOCKET) {
        cleanup();
        return 4;
    }

    // Get the AcceptEx function pointer
    GUID guidAcceptEx = WSAID_ACCEPTEX;
    DWORD bytes = 0;
    if (WSAIoctl(g_listen_socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx, sizeof(guidAcceptEx), &lpfnAcceptEx, sizeof(lpfnAcceptEx), &bytes, NULL, NULL) == SOCKET_ERROR) {
        cleanup();
        return 5;
    }
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    inet_pton(AF_INET, addr, &serv_addr.sin_addr);
    serv_addr.sin_port = htons(port);

    // Bind the socket
    if (bind(g_listen_socket, (SOCKADDR*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        cleanup();
        return 6;
    }

    // Listen for incoming connections
    if (listen(g_listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        cleanup();
        return 7;
    }

    // Start worker threads
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    g_num_threads = sys_info.dwNumberOfProcessors * 2;
    if (g_num_threads > MAX_THREADS) {
        g_num_threads = MAX_THREADS;
    }

    for (int i = 0; i < g_num_threads; ++i) {
        g_worker_threads[i] = CreateThread(NULL, 0, WorkerThread, NULL, 0, NULL);
    }

    return 0;
}

/**
 * Starts the server by posting the first accept operation.
 */
void start_server() {
    post_accept();
}

/**
 * Gracefully shuts down the server.
 * This function signals all worker threads to exit and then waits for them to terminate.
 */
void shutdown_server() {
    g_shutdown_flag = 1;
    for (int i = 0; i < g_num_threads; ++i) {
        PostQueuedCompletionStatus(g_completion_port, 0, 0, NULL);
    }
    WaitForMultipleObjects(g_num_threads, g_worker_threads, TRUE, INFINITE);
    cleanup();
}

/**
 * The main loop for a worker thread. It waits for I/O completion notifications.
 */
DWORD WINAPI WorkerThread(LPVOID lpParam) {
    DWORD bytes_transferred;
    LPPER_HANDLE_DATA per_handle_data;
    LPPER_IO_DATA per_io_data;

    while (g_shutdown_flag == 0) {
        // Wait for an I/O completion notification
        if (GetQueuedCompletionStatus(
            g_completion_port,
            &bytes_transferred,
            (PULONG_PTR)&per_handle_data,
            (LPOVERLAPPED*)&per_io_data,
            INFINITE
        ) == 0) {
            // Check for errors or shutdown signal
            if (per_io_data == NULL) {
                // This is a shutdown signal
                break;
            }
        }

        // Process the completed I/O operation
        process_io(per_handle_data, per_io_data, bytes_transferred);
    }

    return 0;
}

/**
 * Processes a completed I/O operation based on its type.
 */
void process_io(LPPER_HANDLE_DATA per_handle_data, LPPER_IO_DATA per_io_data, DWORD bytes_transferred) {
    switch (per_io_data->operation_type) {
        case IO_ACCEPT:
            // Get the client socket from the per-handle data
            SOCKET client_socket = per_handle_data->socket;
            
            // Post the next accept operation to handle more connections
            post_accept();
            
            // Associate the accepted socket with the completion port
            per_handle_data->socket = client_socket;
            if (CreateIoCompletionPort((HANDLE)per_handle_data->socket, g_completion_port, (ULONG_PTR)per_handle_data, 0) == NULL) {
                closesocket(per_handle_data->socket);
                HeapFree(GetProcessHeap(), 0, per_handle_data);
            } else {
                // Post the first read operation on the newly accepted socket
                post_read(per_handle_data);
            }
            break;

        case IO_READ:
            if (bytes_transferred == 0) {
                // Connection closed by client
                closesocket(per_handle_data->socket);
                HeapFree(GetProcessHeap(), 0, per_handle_data);
                HeapFree(GetProcessHeap(), 0, per_io_data);
                break;
            }
            
            // Call the Rust callback to process the data
            if (g_callback_func != NULL) {
                g_callback_func(per_handle_data, per_io_data->buffer, bytes_transferred);
            }
            
            // For a simple echo server, post a write operation to send the data back
            post_write(per_handle_data, per_io_data->buffer, bytes_transferred);
            break;

        case IO_WRITE:
            // Write operation completed, now we can free the I/O data
            HeapFree(GetProcessHeap(), 0, per_io_data);
            // After writing, we should post another read to wait for the next request
            post_read(per_handle_data);
            break;
    }
}

/**
 * Posts an asynchronous accept operation using AcceptEx.
 */
void post_accept() {
    LPPER_HANDLE_DATA per_handle_data = (LPPER_HANDLE_DATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PER_HANDLE_DATA));
    per_handle_data->socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

    LPPER_IO_DATA per_io_data = (LPPER_IO_DATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PER_IO_DATA));
    per_io_data->operation_type = IO_ACCEPT;
    per_io_data->data_buf.buf = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (sizeof(struct sockaddr_in) + 16) * 2);
    per_io_data->data_buf.len = (sizeof(struct sockaddr_in) + 16) * 2;

    if (lpfnAcceptEx(g_listen_socket, per_handle_data->socket, per_io_data->data_buf.buf, 0, sizeof(struct sockaddr_in) + 16, sizeof(struct sockaddr_in) + 16, NULL, &per_io_data->overlapped) == FALSE && WSAGetLastError() != WSA_IO_PENDING) {
        fprintf(stderr, "AcceptEx failed with error: %d\n", WSAGetLastError());
        closesocket(per_handle_data->socket);
        HeapFree(GetProcessHeap(), 0, per_handle_data);
        HeapFree(GetProcessHeap(), 0, per_io_data->data_buf.buf);
        HeapFree(GetProcessHeap(), 0, per_io_data);
    }
}

/**
 * Posts an asynchronous read operation on a client socket.
 */
void post_read(LPPER_HANDLE_DATA per_handle_data) {
    DWORD flags = 0;
    LPPER_IO_DATA per_io_data = (LPPER_IO_DATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PER_IO_DATA));
    per_io_data->operation_type = IO_READ;
    per_io_data->data_buf.buf = per_io_data->buffer;
    per_io_data->data_buf.len = BUFFER_SIZE;

    if (WSARecv(per_handle_data->socket, &per_io_data->data_buf, 1, NULL, &flags, &per_io_data->overlapped, NULL) == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        closesocket(per_handle_data->socket);
        HeapFree(GetProcessHeap(), 0, per_handle_data);
        HeapFree(GetProcessHeap(), 0, per_io_data);
    }
}

/**
 * Posts an asynchronous write operation to a client socket.
 */
void post_write(LPPER_HANDLE_DATA per_handle_data, const char* data, int data_len) {
    LPPER_IO_DATA per_io_data = (LPPER_IO_DATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PER_IO_DATA));
    per_io_data->operation_type = IO_WRITE;
    per_io_data->data_buf.buf = (char*)HeapAlloc(GetProcessHeap(), 0, data_len);
    if (per_io_data->data_buf.buf) {
        memcpy(per_io_data->data_buf.buf, data, data_len);
        per_io_data->data_buf.len = data_len;
        if (WSASend(per_handle_data->socket, &per_io_data->data_buf, 1, NULL, 0, &per_io_data->overlapped, NULL) == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
            fprintf(stderr, "WSASend failed with error: %d\n", WSAGetLastError());
            HeapFree(GetProcessHeap(), 0, per_io_data->data_buf.buf);
            HeapFree(GetProcessHeap(), 0, per_io_data);
        }
    }
}

/**
 * Cleans up all resources.
 */
void cleanup() {
    if (g_listen_socket != INVALID_SOCKET) {
        closesocket(g_listen_socket);
        g_listen_socket = INVALID_SOCKET;
    }
    if (g_completion_port != NULL) {
        CloseHandle(g_completion_port);
        g_completion_port = NULL;
    }
    WSACleanup();
}
