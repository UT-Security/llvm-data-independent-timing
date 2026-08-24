// Self-contained example with no external library calls
// All functions are defined here so LLVM can analyze them

// Simple string copy function
__attribute__((noinline))
char* my_strcpy(char* dest, const char* src) {
    char* original_dest = dest;
    while (*src != '\0') {
        *dest = *src;
        dest++;
        src++;
    }
    *dest = '\0';
    return original_dest;
}

// Simple string length function
__attribute__((noinline))
int my_strlen(const char* str) {
    int len = 0;
    while (*str != '\0') {
        len++;
        str++;
    }
    return len;
}

// Example 1: Simple taint propagation
int process_input(int user_input) {
    int result = user_input * 2;  // user_input is tainted
    return result + 10;           // result is also tainted
}

// Example 2: String manipulation with tainted data
void process_string(char *user_input) {
    char buffer[100];
    my_strcpy(buffer, user_input);   // Taint flows through my_strcpy
    int len = my_strlen(buffer);     // len depends on tainted data
    buffer[0] = 'X';                 // Modifying tainted buffer
}

// Example 3: Interprocedural propagation
int helper(int x) {
    return x * 3;
}

int compute(int tainted_arg) {
    int result = helper(tainted_arg);  // Taint propagates through call
    return result + 5;
}

// Example 4: Multiple levels of calls
int level3(int val) {
    return val + 1;
}

int level2(int val) {
    return level3(val) * 2;
}

int level1(int user_val) {
    return level2(user_val) + 10;
}

// Example 5: Conditional taint flow
int conditional_process(int secret, int flag) {
    int result;
    if (flag > 0) {
        result = secret * 2;     // Tainted in this branch
    } else {
        result = 100;            // Not tainted in this branch
    }
    return result;               // May or may not be tainted
}

// Example 6: Array operations
void array_process(int *arr, int tainted_index) {
    arr[tainted_index] = 42;     // Tainted index used
    int val = arr[0];            // Array access
    arr[1] = val + 10;
}

// Main function
int main() {
    // Test simple propagation
    int user_val = 10;
    int processed = process_input(user_val);

    // Test string operations
    char input[] = "user_data";
    process_string(input);

    // Test nested calls
    int nested_result = level1(user_val);

    // Test conditional
    int cond_result = conditional_process(user_val, 1);

    // Test array
    int arr[10];
    array_process(arr, user_val);

    return processed + nested_result + cond_result;
}
