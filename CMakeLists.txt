cmake_minimum_required(VERSION 3.20)

project("stm32_hal_ssd1306" C CXX ASM)

include(cmake/st-project.cmake)

add_executable(${PROJECT_NAME})
add_st_target_properties(${PROJECT_NAME})

# 用户新增的源文件（st-project.cmake 由工具自动生成，不改它）
target_sources(${PROJECT_NAME} PRIVATE
    Core/Src/max30102.c
    Core/Src/dht11.c
    Core/Src/light.c
    Core/Src/ui.c
    Core/Src/i2c1.c
    Core/Src/servo.c
    Core/Src/buzzer.c
    Core/Src/mpu6050.c
    Core/Src/speed.c
    Core/Src/adc1.c
    Core/Src/knob.c
    Core/Src/encoder.c
)