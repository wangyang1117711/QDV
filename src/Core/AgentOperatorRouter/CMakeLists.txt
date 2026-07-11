# AgentOperatorRouter - Agent协作框架算子路由组件
# 编译为静态库，供主程序和测试链接

add_library(AgentOperatorRouter STATIC
    AgentOperatorRouter.h
    AgentOperatorRouter.cpp
)

target_include_directories(AgentOperatorRouter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(AgentOperatorRouter PUBLIC
    Qt6::Core
)

target_compile_features(AgentOperatorRouter PUBLIC cxx_std_17)
