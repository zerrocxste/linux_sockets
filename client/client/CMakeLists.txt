# CMakeList.txt: проект CMake для client; включите исходный код и определения,
# укажите здесь логику для конкретного проекта.
#
cmake_minimum_required (VERSION 3.8)

# Добавьте источник в исполняемый файл этого проекта.
add_executable (client "client.cpp" "client.h")

# TODO: Добавьте тесты и целевые объекты, если это необходимо.
