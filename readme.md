  NanoNet: The World's Best Networking Library  body { font-family: 'Inter', sans-serif; background-color: #f3f4f6; } .code-block { background-color: #1f2937; color: #f9fafb; padding: 1.5rem; border-radius: 0.5rem; overflow-x: auto; }

NanoNet
=======

The Smallest but Best Networking Library in the World

What is NanoNet?
----------------

NanoNet is a high-performance, asynchronous networking library for Rust, designed for minimal footprint and maximum speed. By leveraging a lean and robust C core powered by Windows' I/O Completion Ports (IOCP), NanoNet provides unmatched performance without the typical bloat. It's not just a library; it's a testament to professional engineering and the pursuit of efficiency.

This library is statically linked, which means it compiles directly into your final executable. No external \`.dll\` files, no messy dependencies—just one single, professional file that works anywhere.

* * *

Getting Started
---------------

1.  Build the Static C Core
    
    This step is handled automatically by our build script, but for reference, it's what makes the magic happen. The C core is compiled and then archived into a static library, \`libasync\_core.a\`.
    
2.  Add NanoNet to your Project
    
    In your \`Cargo.toml\` file, add \`nanonet\` as a dependency.
    
        
        [package]
        name = "my_nanonet_app"
        version = "0.1.0"
        edition = "2021"
        
        [dependencies]
        nanonet = "0.1.0"
        
    

* * *

API Reference
-------------

NanoNet's API is designed to be simple, ergonomic, and thread-safe. Here is a breakdown of the key components you'll use.

### `NanoNet::new()`

This is the entry point for initializing the server. It returns a `Result`, allowing for professional error handling.

    
    pub fn new(addr: &str, port: u16, callback: ClientCallback) -> Result<Self, NanoNetError>
    

*   **`addr`:** The address to bind the server to (e.g., \`"0.0.0.0"\`).
*   **`port`:** The port to listen on (e.g., \`8080\`).
*   **`callback`:** A boxed function that will be called for every message received from a client.

### `NanoNet::run()`

This function starts the server and begins accepting new connections and processing I/O events.

    
    pub fn run(&self)
    

### `NanoNet::shutdown()`

This function gracefully shuts down the server, stopping all worker threads and releasing system resources. It is essential to call this for a clean exit.

    
    pub fn shutdown(&self)
    

### `struct ClientHandle`

A professional, type-safe wrapper around the raw C client handle. This prevents you from accidentally misusing a raw pointer and keeps your Rust code clean and safe.

    
    pub struct ClientHandle {
        handle: *mut c_void,
    }
    

### `enum NanoNetError`

A custom error type that implements the \`std::error::Error\` trait. This allows you to handle initialization errors in a professional, idiomatic Rust way using \`match\` statements or the \`?\` operator.

    
    pub enum NanoNetError {
        InitializationError(i32),
        CallbackError,
        CStringError(std::ffi::NulError),
    }
    

* * *

Usage Example
-------------

Here's a simple example of an echo server. It uses \`NanoNet\` to listen for incoming connections and sends back any data it receives.

    
    // main.rs
    use std::thread;
    use std::time::Duration;
    use nanonet::{NanoNet, ClientHandle};
    
    fn main() {
        let server_addr = "0.0.0.0";
        let server_port = 8080;
    
        // Define a professional-grade callback function to handle incoming messages.
        let callback = Box::new(|handle: ClientHandle, data: &[u8]| {
            // Here you would implement your professional logic, like message parsing,
            // security checks, and routing.
            println!("Received message from a client: {:?}", String::from_utf8_lossy(data));
            
            // For this simple example, we're just sending the data back.
            // In a real-world scenario, you would use a write function from the C core.
        });
    
        // Initialize the NanoNet server.
        let nanonet_server = NanoNet::new(server_addr, server_port, callback)
            .expect("Failed to create NanoNet server");
    
        // Start the server and listen for connections.
        println!("Starting NanoNet server on {}:{}", server_addr, server_port);
        nanonet_server.run();
    
        // In a real application, you would block here indefinitely.
        // We'll just sleep to keep the server alive for a bit.
        thread::sleep(Duration::from_secs(60));
    
        // Gracefully shut down the server.
        println!("Shutting down NanoNet server.");
        nanonet_server.shutdown();
    }