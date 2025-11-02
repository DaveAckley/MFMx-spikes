# T6.py

class T6:
    def __init__(self, tlbi):
        self.tlbi = tlbi

    def __repr__(self):
        # Using __repr__ for consistent output in lists/debugging
        return f"<T6:{self.tlbi}>"

    def __str__(self):
        # Human-readable string representation
        return f"T6 object with tlbi={self.tlbi}"

# Example usage (optional, for testing T6 in isolation)
if __name__ == "__main__":
    t6_obj = T6(5)
    print(t6_obj)
    print(repr(t6_obj))
    
