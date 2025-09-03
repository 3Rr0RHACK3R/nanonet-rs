#ifndef ASYNC_CORE_H
#define ASYNC_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

// A professional-grade callback function for Rust to hook into the C core.
typedef int (*NanoNetCallback)(void* client_handle, const char* data, int data_len);

// API functions
extern int initialize_server(const char* addr, unsigned short port, NanoNetCallback callback_func);
extern void start_server();
extern void shutdown_server();

#ifdef __cplusplus
}
#endif

#endif // ASYNC_CORE_H
