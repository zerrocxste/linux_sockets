# CMakeList.txt: проект CMake для sock_example; включите исходный код и определения,
# укажите здесь логику для конкретного проекта.
#
cmake_minimum_required (VERSION 3.8)

# Добавьте источник в исполняемый файл этого проекта.
add_executable (sock_example "sock_example.cpp" "sock_example.h")

# TODO: Добавьте тесты и целевые объекты, если это необходимо.