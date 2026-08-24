set(PLATFORM_SOURCES sys/linux/LinuxCap.cpp sys/linux/SealedStore_linux.cpp)
set(PLATFORM_LIBRARIES dl)
# Отдельными переменными: тестовой цели нужна только запечатка, без GUI-частей.
set(SEALED_STORE_SOURCE sys/linux/SealedStore_linux.cpp)
set(SEALED_STORE_LIBRARIES)
