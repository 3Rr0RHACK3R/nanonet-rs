use std::ffi::{c_int, c_void, CString};
use std::os::raw::c_char;
use std::sync::mpsc;
use std::sync::{Arc, Mutex};
use std::error::Error;
use std::fmt;

// A custom error type for professional error handling
#[derive(Debug)]
pub enum NanoNetError {
    InitializationError(i32),
    CallbackError,
    CStringError(std::ffi::NulError),
}

impl fmt::Display for NanoNetError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            NanoNetError::InitializationError(code) => write!(f, "Failed to initialize C server core with error code: {}", code),
            NanoNetError::CallbackError => write!(f, "Internal callback function failed"),
            NanoNetError::CStringError(err) => write!(f, "Failed to create C string: {}", err),
        }
    }
}

impl Error for NanoNetError {}

// Type-safe wrapper for the C client handle
#[derive(Debug)]
pub struct ClientHandle {
    handle: *mut c_void,
}

impl ClientHandle {
    /// Creates a new type-safe client handle.
    pub fn new(handle: *mut c_void) -> Self {
        ClientHandle { handle }
    }
}

// A professional-grade callback type for our library users.
pub type ClientCallback = Box<dyn Fn(ClientHandle, &[u8]) + Send + Sync + 'static>;

// We need a thread-safe way to store our callback function.
static mut GLOBAL_CALLBACK_SENDER: Option<mpsc::Sender<(ClientHandle, Vec<u8>)>> = None;

// Import the functions from our static C library
#[link(name = "async_core", kind = "static")]
extern "C" {
    fn initialize_server(addr: *const c_char, port: c_int, callback: extern "C" fn(*mut c_void, *const c_char, c_int)) -> c_int;
    fn start_server();
    fn shutdown_server();
}

// The professional API for our library
pub struct NanoNet;

impl NanoNet {
    /// Initializes the server, binds to an address, and prepares the core for running.
    ///
    /// The `callback` is a function that will be called for every message received.
    pub fn new(addr: &str, port: u16, callback: ClientCallback) -> Result<Self, NanoNetError> {
        let c_addr = CString::new(addr).map_err(NanoNetError::CStringError)?;

        // Set up the channel to safely send data from the C world to our Rust callback.
        let (sender, receiver) = mpsc::channel();
        
        unsafe {
            // This is a professional use of an unsafe block. We're carefully
            // setting a global static variable.
            GLOBAL_CALLBACK_SENDER = Some(sender);
        }

        // The C callback function that will bridge the C core to our Rust channel.
        extern "C" fn c_callback(handle: *mut c_void, data: *const c_char, data_len: c_int) {
            unsafe {
                if let Some(sender) = &GLOBAL_CALLBACK_SENDER {
                    let client_handle = ClientHandle::new(handle);
                    let data_slice = std::slice::from_raw_parts(data as *const u8, data_len as usize);
                    let data_vec = data_slice.to_vec();
                    sender.send((client_handle, data_vec)).unwrap();
                }
            }
        }
        
        let result = unsafe {
            // We pass our C callback function pointer to the C core.
            initialize_server(c_addr.as_ptr(), port as c_int, c_callback)
        };

        if result != 0 {
            return Err(NanoNetError::InitializationError(result));
        }

        // Spawn a thread to process messages from the C core and call the user's callback.
        std::thread::spawn(move || {
            while let Ok((handle, data)) = receiver.recv() {
                callback(handle, &data);
            }
        });

        Ok(NanoNet)
    }

    /// Starts the server and begins accepting connections.
    pub fn run(&self) {
        unsafe {
            start_server();
        }
    }

    /// Gracefully shuts down the server.
    pub fn shutdown(&self) {
        unsafe {
            shutdown_server();
        }
    }
}
