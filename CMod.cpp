#include <pybind11/pybind11.h>
#include <pybind11/functional.h> // For py::function
#include <thread>
#include <iostream>
#include <mutex>
#include <chrono> // For std::chrono::seconds
#include <atomic> // For std::atomic<bool>

namespace py = pybind11;

class CMod {
public:
    CMod() : thread_should_run(false) {}

    // Registers a Python function to be called from C++
    void register_pynotify(py::function pynotify_func) {
        // Protects access to py_notify_callback
        std::lock_guard<std::mutex> lock(callback_mutex); 
        py_notify_callback = pynotify_func;
        std::cout << "C++: Python 'pynotify' function registered." << std::endl;
    }

    // The C++ worker thread's main function
    void runThread() {
        std::cout << "C++ thread started." << std::endl;

        // Loop as long as the atomic flag indicates the thread should run
        while (thread_should_run.load()) {
            // Perform C++ work. The GIL is not held here.
            std::this_thread::sleep_for(std::chrono::seconds(1)); 

            // Acquire C++ mutex to protect the callback object (py_notify_callback)
            // The GIL is still released at this point.
            std::lock_guard<std::mutex> lock(callback_mutex); 

            if (py_notify_callback) {
                // Acquire the GIL *just before* the Python call, within this tight scope.
                py::gil_scoped_acquire acquire_gil_for_python_call; 

                try {
                    py_notify_callback("Notification from C++ thread!"); 
                } catch (const py::error_already_set& e) {
                    std::cerr << "C++: Error calling pynotify: " << e.what() << std::endl;
                    PyErr_Print();
                }
            } else {
                std::cout << "C++: pynotify callback not registered." << std::endl;
            }
            // 'acquire_gil_for_python_call' (GIL) is released here if it was created.
            // 'lock' (callback_mutex) is released here.
            // The GIL is not held by the C++ thread at this point.
        }
        std::cout << "C++ thread finished." << std::endl;
        // The thread exits. Since the GIL was never acquired globally for this thread,
        // there's no GIL to release here, preventing deadlocks during shutdown.
    }

    // Python-callable function to control the C++ thread's state
    void changeState(int phase) {
        // Protects access to thread_ptr and thread_should_run (for thread creation/joining)
        std::unique_lock<std::mutex> lock(thread_mutex); 

        if (phase > 5) {
            if (!thread_should_run.load()) { // Check if a thread is not already running
                std::cout << "changeState: Spawning new C++ thread." << std::endl;
                thread_should_run.store(true); // Signal the thread to start its loop
                thread_ptr = std::make_unique<std::thread>(&CMod::runThread, this);
            } else {
                std::cout << "changeState: Thread already running, not spawning a new one." << std::endl;
            }
        } else if (phase < 3) {
            if (thread_ptr && thread_ptr->joinable()) { // Check if a thread exists and is joinable
                std::cout << "changeState: Signalling C++ thread to stop and joining." << std::endl;
                thread_should_run.store(false); // Signal the thread to stop its loop

                lock.unlock(); // Release C++ mutex BEFORE joining to prevent deadlock

                // Release the GIL before calling join()
                // This allows the Python interpreter (main thread) to run while we wait for the C++ thread.
                py::gil_scoped_release release_gil_for_join; 

                thread_ptr->join(); // Block until C++ thread finishes

                // GIL is re-acquired when 'release_gil_for_join' goes out of scope
                lock.lock(); // Re-acquire C++ mutex
                thread_ptr.reset(); // Remove the thread
            } else {
                std::cout << "changeState: No C++ thread to join." << std::endl;
            }
        } else { // phase >= 3 and <= 5
            std::cout << "changeState: Phase " << phase << ", doing nothing." << std::endl;
        }
    }

    // Destructor to ensure any running thread is joined cleanly
    ~CMod() {
        std::unique_lock<std::mutex> lock(thread_mutex); 
        if (thread_ptr && thread_ptr->joinable()) {
            std::cout << "CMod destructor: Signalling C++ thread to stop and joining." << std::endl;
            thread_should_run.store(false); // Signal the thread to stop

            lock.unlock(); // Release C++ mutex before joining

            // Release the GIL in the destructor as well before joining
            py::gil_scoped_release release_gil_for_dtor_join; 

            thread_ptr->join();

            lock.lock(); // Re-acquire C++ mutex
            thread_ptr.reset();
        }
    }

private:
    std::unique_ptr<std::thread> thread_ptr; // Manages the C++ worker thread
    std::atomic<bool> thread_should_run;     // Atomic flag to signal the C++ thread to run/stop
    std::mutex thread_mutex;                 // Protects thread_ptr and thread_should_run access during creation/joining

    py::function py_notify_callback;         // Stores the Python callable
    std::mutex callback_mutex;               // Protects access to py_notify_callback
};

PYBIND11_MODULE(CMod, m) {
    m.doc() = "pybind11 example plugin";

    py::class_<CMod>(m, "CMod")
        .def(py::init<>())
        .def("changeState", &CMod::changeState, "A function that changes the CMod state based on phase.")
        .def("register_pynotify", &CMod::register_pynotify, "Registers a Python function to be called from C++.");
}
