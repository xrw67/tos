#ifdef _WIN32
#define TOS_TEST_EXPORT __declspec(dllexport)
#else
#define TOS_TEST_EXPORT __attribute__((visibility("default")))
#endif

extern "C" TOS_TEST_EXPORT int TosDynamicLibraryIncrement(int value) { return value + 1; }

extern "C" {
TOS_TEST_EXPORT int tos_dynamic_library_test_value = 42;
}
