add_executable(eam-walkthrough EamWalkthrough.cpp)
target_link_libraries(eam-walkthrough PRIVATE ${EUCLID_CDK_DEFAULT_TARGET})
set_target_properties(eam-walkthrough PROPERTIES CXX_SCAN_FOR_MODULES OFF)
