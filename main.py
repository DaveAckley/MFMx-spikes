import CMod
import time
import threading

# This is the Python function that C++ will call
def pynotify(message):
    # print(f"Python received notification: {message}")
    # For debugging GIL issues, sometimes just printing can be enough,
    # but more complex Python operations here would highlight the problem.
    # Let's add a small delay to simulate work and increase chances of contention.
    print(f"Python: {threading.current_thread().name} received notification: {message}")
    time.sleep(0.01) # Simulate some Python work

def main():
    cmod_instance = CMod.CMod()

    # Register the Python function with the C++ module
    cmod_instance.register_pynotify(pynotify)

    print("--- Phase 0: No thread to join ---")
    cmod_instance.changeState(0)

    print("\n--- Phase 6: Spawn a new thread (will call pynotify) ---")
    cmod_instance.changeState(6)
    time.sleep(3.5) # Let the C++ thread run and call pynotify a few times

    print("\n--- Phase 1: Join the existing thread ---")
    cmod_instance.changeState(1)

    print("\n--- Phase 4: Do nothing ---")
    cmod_instance.changeState(4)

    print("\n--- Phase 7: Spawn another thread ---")
    cmod_instance.changeState(7)
    time.sleep(2.5) # Let it run again

    print("\n--- Phase 8: Try to spawn another thread (should not happen if one is running) ---")
    cmod_instance.changeState(8)

    print("\n--- Phase 2: Join the existing thread ---")
    cmod_instance.changeState(2)

    print("\n--- End of script ---")

    
if __name__ == "__main__":
    main()
