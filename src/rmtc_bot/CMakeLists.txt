cmake_minimum_required(VERSION 3.8)
project(rmtc_bot CXX)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX)
  add_compile_options(-Wall -Wextra)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(jsoncpp REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(CURL REQUIRED)

if(TARGET jsoncpp_lib)
  set(JSONCPP_TARGET jsoncpp_lib)
elseif(TARGET JsonCpp::JsonCpp)
  set(JSONCPP_TARGET JsonCpp::JsonCpp)
else()
  set(JSONCPP_TARGET jsoncpp)
endif()

# 纯逻辑核心库：不依赖 rclcpp，便于单元测试
set(CORE_SOURCES
  src/types.cpp
  src/utils/logger.cpp
  src/utils/time_window.cpp
  src/utils/config_loader.cpp
  src/perception/relative_map.cpp
  src/perception/parser.cpp
  src/perception/state_store.cpp
  src/perception/entity_tracker.cpp
  src/planning/grid_map.cpp
  src/planning/path_planner.cpp
  src/planning/combat_rules.cpp
  src/planning/target_selector.cpp
  src/action/action_schema.cpp
  src/action/action_sender.cpp
  src/action/last_action.cpp
  src/ai/event_memory.cpp
  src/ai/prompt_builder.cpp
  src/ai/output_validator.cpp
  src/ai/quota_manager.cpp
  src/ai/llm_client.cpp
  src/events/event_input.cpp
  src/events/mission_input.cpp
  src/events/event_state.cpp
  src/decision/safety_guard.cpp
  src/decision/stage1_collect.cpp
  src/decision/stage2_combat.cpp
  src/decision/local_fallback.cpp
  src/decision/decision_router.cpp
)

add_library(rmtc_bot_core STATIC ${CORE_SOURCES})
target_include_directories(rmtc_bot_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(rmtc_bot_core PUBLIC
  ${JSONCPP_TARGET}
  yaml-cpp
  CURL::libcurl
)

add_executable(main_node
  src/main.cpp
  src/main_node.cpp
  src/registration/team_registry.cpp
)
target_link_libraries(main_node
  rmtc_bot_core
  rclcpp::rclcpp
  std_msgs::std_msgs
  ament_index_cpp::ament_index_cpp
)

install(TARGETS main_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})
install(PROGRAMS scripts/run_contestant.sh DESTINATION share/${PROJECT_NAME}/scripts)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  set(TEST_SOURCES
    test_relative_position
    test_perception_parse
    test_action_schema
    test_planning
    test_event_text_no_regex
    test_llm_fallback
    test_stage2_tactics
  )
  foreach(test_name ${TEST_SOURCES})
    ament_add_gtest(${test_name} test/${test_name}.cpp)
    if(TARGET ${test_name})
      target_link_libraries(${test_name} rmtc_bot_core)
      target_compile_definitions(${test_name} PRIVATE
        RMTC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
  endforeach()
endif()

ament_package()
